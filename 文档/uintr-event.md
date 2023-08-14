## 问题与思路

问题:

- 读者写者问题, 共享内存, eventfd+epoll 通知
- 多核 CPU, 读写进程可能在不同核心.上下文切换成本更高.

使用 uintr 解决问题的思路:

思路1

- uintr 无法直接干涉 读者进程的调度,但借鉴 `uintr_receiver_wait` 的思路,可以节省一些 写者发通知的成本,缓解这个问题.
- 写者调用 `_senduipi(uipi_index);` 后, 硬件会比较中断向量与寄存器(IA32_UINTR_MISC 的 39:32 bit),是否一致,不一致则正常触发内核中断. 进入内核中断处理后, 可以对接到 eventfd 等机制, 完成通知传递.
- 在上述过程中,写者发通知成本由硬件执行, 因此有可能比写者进程直接通过系统调用发通知更快.

思路2

- 完全放弃 eventfd + epoll 的形式,改为使用 中断形式处理通知. uintr 有可能带来数倍的性能提升.
- 现有实现中,当读者进程挂起时, 用户中断会触发一次系统中断,进入系统中断,记录未被执行的用户中断(称为虚假用户中断).
- 针对读者进程挂起问题, 有可能在 虚假用户中断中,类似 思路 1,直接唤醒读者.
  - [poc_v2](https://github.com/intel/uintr-linux-kernel/blob/uintr-next/arch/x86/kernel/irq.c#L381) 有类似的处理.

思路2 改造已有业务涉及范围太大, 先实现并评估 思路 1.

## 设计与实现

要求: 按照思路 1,实现基于 uintr 的通知机制

- 适配 epoll 管理,方便使用.
- 用户中断不应干扰读者进程其他处理流程.

参考 `uintr_receiver_wait` 的思路,使用 eventfd 作为通知机制.

- 增加新的系统中断 及 中断处理函数. 在中断处理函数中写入 eventfd 通知.
- 增加新的系统调用, 创建应用层和内核的关联.
- 调整默认用户中断行为,防止干扰读者进程其他事务.

### 代码实现

源码在 [epoll_waitlist](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/tree/epoll_waitlist?ref_type=heads) 分支.

[add UINTR_EVENT_VECTOR](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/commit/d1528589b422729a8f9b10fce153425bc8ed5949): 增加新的系统调用 和 中断处理函数, 声明 `UINTR_EVENT_VECTOR` 地方比较多,参考 `UINTR_KERNEL_VECTOR` 小心添加.

```c
#define UINTR_EVENT_VECTOR  0xe9

DEFINE_IDTENTRY_SYSVEC(sysvec_uintr_event_notification)
{
	ack_APIC_irq();
	inc_irq_stat(uintr_event_notifications);
	/* do event things */ 
	uintr_event_write();
}
```

[add system call uintr_event(int fd)](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/commit/c6c83e45719d3774bf723536f323e317eb9a384b): 增加新的系统调用, 读者进程首先创建 eventfd 的文件描述符,再通过 `uintr_event` 调用传递给内核.

最终内核维护 [eventfd_ctx 和 等待 list](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/blob/epoll_waitlist/arch/x86/kernel/uintr_core.c#L62)

```c
/* for uint_event*/
static DEFINE_SPINLOCK(uintr_event_lock);
static struct eventfd_ctx *uintr_event_ctx = NULL;
static struct list_head uintr_event_list = LIST_HEAD_INIT(uintr_event_list);
```

- 通过 uintr_event_ctx 关联到应用层通知.
- 收到 uintr-evet 用户中断时,中断处理程序调用 `uintr_event_write`,写入一个字节,通知读者进程.
- `uintr_event_write` 还有一个重要流程是通过 `uintr_event_list` 置位对应 UPID 的 ON 位置, 在标准流程中 ON 置位由硬件完成,这里必须软件置位.

[调整默认用户中断行为](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/commit/aadb3fd0fd2c1b4dab3e68fc9bee8a98f0afb12c): 注意,这个提交使 uintr 无法再接收用户中断了.

- 读者进程挂起时, 不再置位 SN, [`switch_uintr_prepare`](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/blob/epoll_waitlist/arch/x86/kernel/uintr_core.c#L903);
- 读者进程回到前台时, 不再更换 upid 的通知向量,不再发起 `send_IPI_self` 触发用户中断处理. [`switch_uintr_return`](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/blob/epoll_waitlist/arch/x86/kernel/uintr_core.c#L979)

#### qemu bug 修正

可能与 qemu 实现有关,已有代码中不得不处理寄存器丢失置位等情况, 基于硬件测试时请去除相关代码,防止干扰.

ON 置位:

- 问题: 理论上 ON 仅在写者发送时,置位一次, 当在测试中,依然存在无故置位情况, 导致 硬件无法发送用户中断.特别是当读者进程挂起后,发生频率更高.
- 解决: 可能的地方,多次重置 ON,目前是在 进程切换到后台时,增加一次重置 ON;
- [代码](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/blob/epoll_waitlist/arch/x86/kernel/uintr_core.c#L904)

UINTR_TT 中 uitt 地址无故清空

- 问题: 当写者进程因调度切换运行核心时, UINTR_TT 寄存器重置为全 0.导致 用户中断发送到错误地址.
- 解决: 用户进程回到前台之前,比较 UINTR_TT 寄存器内 uitt 地址与实际 uitt 地址是否相同,不相同则重写.
- [代码](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/blob/epoll_waitlist/arch/x86/kernel/uintr_core.c#L918)

写者执行 `_senduipi` 但无输出, 过一点时间又恢复正常.

- 问题: 写者进程因调度出现了核心切换,其他核心没有置位 UIF,导致写者在这个核心运行时无法发送用户中断.
- 解决: 限制 qemu 模拟双核 cpu,在读者 写者进程分别都执行一次 `_stui()`.这个问题应该是 qemu 对 uintr 实现并不完整导致.
- 在测试代码中,与内核无关.

## 性能测试

测试程序在 [uintr-event](https://git.mesalab.cn/zhangyang/uintr-event) 下的 `/test` 文件夹

- 测试部分源码来自 ipc-bench

比较 uintr-event  和 eventfd 在相同场景下的性能

- 共享内存 epoll 等
- 取时间,多次复测 重启 qemu; 保证编译命令相同;
- 写者进程 write 前时间,写入共享内存, 读者唤醒后取结束时间.
  - 单独测量了 读者挂起,从写者 write 到 读者唤醒,这个场景下时间..
  - 写者发通知前增加了 100ms 的延迟,以等待读者挂起.(100ms 大于任何已测得的数据)
- 跳过失败的,只统计成功记录.

编译命令如下

```c
gcc-11 -O2 -Wall -static -muintr -minline-all-stringops -std=c11 ./shm-eventfd-bi.c  -lpthread -o ./shm-eventfd-bi -lm

gcc-11 -O2 -Wall -static -muintr -minline-all-stringops -std=c11 ./shm-uintr-event-bi.c -lpthread -o ./shm-uintr-event-bi -lm
```

非阻塞测试 100000,阻塞测试 10000;

| method           | Average(us) | min(us) | max(us)   | rate(msg/s) | Standard deviation |
| ---------------- | ----------- | ------- | --------- | ----------- | ------------------ |
| eventfd          | 93.542      | 20.790  | 34906.550 | 10690       | 298.206            |
| uintr_event      | 89.837      | 31.820  | 7010.990  | 11131       | 60.103             |
| evetfd_wait      | 523.533     | 129.080 | 19615.600 | 1910        | 865.047            |
| uintr_event_wait | 266.885     | 97.140  | 10011.420 | 3746        | 261.965            |

原始记录

```c
============ RESULTS ================
Message size:       1
Message count:      99575
Total duration:     9314.517    ms
Average duration:   93.542      us
Minimum duration:   20.790      us
Maximum duration:   34906.550   us
Standard deviation: 298.206     us
Message rate:       10690       msg/s
=====================================

============ RESULTS ================
Message size:       1
Message count:      99999
Total duration:     8983.693    ms
Average duration:   89.837      us
Minimum duration:   31.820      us
Maximum duration:   7010.990    us
Standard deviation: 60.103      us
Message rate:       11131       msg/s
=====================================

============ RESULTS ================
Message size:       1
Message count:      9792
Total duration:     5126.437    ms
Average duration:   523.533     us
Minimum duration:   129.080     us
Maximum duration:   19615.600   us
Standard deviation: 865.047     us
Message rate:       1910        msg/s
=====================================

============ RESULTS ================
Message size:       1
Message count:      9999
Total duration:     2668.588    ms
Average duration:   266.885     us
Minimum duration:   97.140      us
Maximum duration:   10011.420   us
Standard deviation: 261.965     us
Message rate:       3746        msg/s
=====================================
```

## 结论

基于 qemu 模拟器的数据: 

- 接收者非挂起时, uintr-event 与 evenfd 速度快了4%-6%.
  - 重新挂机测了多组数据 均在 4%-6% 之间, 与 [User_Interrupts_LPC_2021.pdf](https://lpc.events/event/11/contributions/985/attachments/756/1417/User_Interrupts_LPC_2021.pdf) 所给的 User IPI(blocked) 数据大约 7% 非常接近. 倾向于认为 qemu 下 uint-event 已经正常工作了
- 接收者挂起时, uintr-event 唤醒接收者的速度要快 50% 左右.
  - 我对这个结论在物理机能否复现存疑, 幅度应该没有这么夸张.
  - 另外测试程序也需要复核以确保测试结果的准确性.

uintr 结合 eventfd 应当可以作为一个解决 eventfd+epoll 瓶颈的一个思路, 提升效果明显.

- 用户中断 rfc 尚未被通过, 源代码库开发并不活跃 (最后 [commit](https://github.com/intel/uintr-linux-kernel/commit/f20fa04c4553c6140dc8dc49e55fa282d2360ece) 时间是 Sep 13, 2021), 尚不能在生产环境使用.
- 以上测试结果均基于 qemu 模拟器测得, qemu 的实现尚有一些 bug,无法 100%保证 物理机可以复现相同的结果.
- uintr_event 基于 [rfc-v1](https://github.com/intel/uintr-linux-kernel/tree/rfc-v1) 修改, 最新的 [poc_v2](https://github.com/intel/uintr-linux-kernel/tree/poc-v2%3E) 跑 ipc-bench 中有 bug (不确定是 qemu 还是 kernel 的问题).
  - 已有 qemu 要运行,还需要基于这个 [commit](https://github.com/OS-F-4/uintr-linux-kernel/commit/9f6a41b01ad529801838f1bd8175ed76c1509221) 修改 kernel 代码.
