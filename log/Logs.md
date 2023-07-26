### 间隔 1s 发中断,始终会遇到无法再响应唤醒问题

```c
...
receive, cur core:0
receive upid status: 0b000000000
XXXuiret
[  418.415226] uintr_receiver_wait
[  418.415226] wait upid status: 0
        -- User Interru[  418.417581] uintr_receiver_wait: going to sleep
pt handler --
uintr_handler
reader i = 247
reader guard1  w
reader guard2  r
writer guard1  r
writer guard2  r
writer guard3  w
uitte index:0
uitt addr: 0xffff9b4d1a719001  upid addr: 0xffff9b4d1a53b1c0
upid status: 0b000000000
senduipi core: 1 uitte index:0  dist core: 0 ifsend: 1, nv: 235
[  419.415512] UINTR: Kernel received a User Interrupt notification
[  419.416199] uintr_wake_up_process
[  419.416496] uintr_wake_up_process: wake up task 269
[  419.416498] uintr_wake_up_process: done
[  419.418504] sending self ipi
receive, cur core:2
--uif zero,prev:3 | id:2 return
[  419.421611] sending self ipi
receive, cur core:2
--uif zero,prev:3 | id:2 return
[  419.435593] sending self ipi
```

似乎是接收者调度到了另一个核心 core 2,在 `target/i386/tcg/seg_helper.c` 的 `do_interrupt64`

```c
    if(intno == UINTR_UINV ){
        qemu_log("receive, cur core:%d\n",get_apic_id(cpu_get_current_apic()));
        recognized = true;
        cpl = env->hflags & HF_CPL_MASK;
        if(!uif_enable(env)){
            DeviceState *dev = cpu_get_current_apic();
            int id = get_apic_id(dev);
            qemu_log("--uif zero,prev:%d | id:%d return\n",cpl, id);
            rrzero_count +=1; 
            if(rrzero_count > 2000){
                qemu_log("too many zeros, exit\n");
                exit(2);
            }
            helper_clear_eoi(env);
            return;
        }
```

主因是 `if(!uif_enable(env))` 这里 uif 没有开启, 似乎是每个核心单独的结构体

```c
static bool uif_enable(CPUX86State *env){
    return env->uintr_uif != 0;
}
```

控制开关的如下两个函数

```c
void helper_stui(CPUX86State *env){
    qemu_log("stui core: %d\n", get_apic_id(cpu_get_current_apic()));
    switch_uif(env, true);
}
void helper_clui(CPUX86State *env){
    qemu_log("clui core: %d\n", get_apic_id(cpu_get_current_apic()));
    switch_uif(env, false);
}
```

如果限制 2 个核心,然后发送和接收方两个线程都执行 `_stui();` 调用.

###

```c
N = 1000
reader guard  w
stui core: 0
write tt ffff951f5a63b001  core:1
stui core: 1
reader i = 0
reader guard1  w
reader guard2  r
[  712.151285] uintr_receiver_wait
[  712.151599] wait upid status: 0
[  712.151762] uintr_receiver_wait: going to sleep
writer guard1  r
writer guard2  r
writer guard3  w
uitte index:0
uitt addr: 0xffff951f5a63b001  upid addr: 0xffff951f5a6d1180
upid status: 0b000000000
senduipi core: 1 uitte index:0  dist core: 0 ifsend: 1, nv: 235
[  712.154198] UINTR: Kernel received a User Interrupt notification
[  712.154762] uintr_wake_up_process
[  712.154835] uintr_wake_up_process: wake up task 246
[  712.154835] uintr_wake_up_process: done
[  712.156956] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.157649] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
reader i = 1
[  712.158871] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.159727] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.160479] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.162348] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.163208] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.163666] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
reader guard1  w
[  712.164213] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.164487] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
reader guard2  r
[  712.164892] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.165386] sending self ipi
receive, cur core:1
receive upid status: 0b000000000
do not go to handler
[  712.165681] uintr_receiver_wait
[  712.166160] wait upid status: 1
[  712.166385] uintr_receiver_wait: going to sleep
writer guard1  r
writer guard2  r
writer guard3  w
uitte index:0
uitt addr: 0xffff951f5a63b001  upid addr: 0xffff951f5a6d1180
upid status: 0b000000001
senduipi core: 1 uitte index:0  dist core: 1 ifsend: 0, nv: 235
writer guard1  w
```

这会似乎是停留在了 `upid.puir` 里面...

```c
        if(upid.puir != 0){
            env->uintr_rr = upid.puir;
            upid.puir = 0; // clear puir
            send = true;
        }
```

puir 不明原因重置,这个影响了 2 个方面

- [[uintr 流程梳理#^5f2176]] 这里的实现中有提前对 puir 判断, 全 0 会中断处理流程.
- 进程唤醒恢复到前台时候 [[uintr 流程梳理#^43673f]] 也会判断一下.

---

qemu 修改了发送方 notify 的通知,改成每次必发中断

---

shm_uintr4 中有一个可能的死锁条件:

- 进程同步的手段 会 早于 uintr_wait 一点点执行
- 这有可能造成 read 还未休眠 直接中断 了,然后再挂起就死锁了.

这样倒是明白为什么非要包装成 eventfd 或 配合 epoll 能用的形式了..

同步问题引起的坑太大了.

# 7.10

uintr 接收者在前台时候 禁用通知, 挂起后才允许通知.

接收者恢复时候,并不需要 sedIPI ,只需要等待唤醒即可.

nc.nv 不能被替换成 UINTR_NOTIFICATION_VECTOR

写者间隔 1s 正常, 10-100 ms 会出现问题.

# 7.11

```c
[   35.080134] UINTR: Kernel received a User Interrupt notification
[   35.080146] uintr_wake_up_process
[   35.080154] uintr_wake_up_process: test task 92, status 5
[   35.080172] uintr_wake_up_process: wake up task 92, status 7
[   35.080184] uintr_wake_up_process: done
[   35.082130] switch task 92 blocking
[   35.082344] switch status 1 blocking
```

依旧是昨天问题的延续,加了更多的打印,似乎是一次 uintr_wake_up_process 后,从等待队列中删除了,但又没有唤醒,立刻 block 了. 唤醒机会只有这一次,之后再发唤醒也没法响应了.

临时解决办法: 等待队列除非主动退出,否则都在里面.

续:

继续运行N久,进程没有被消失, 不过不能一直在台前,这样的唤醒没有意义.

放弃 demo 转向 实现 epoll 接口看看.

### 汇总

上周到现在的进度:

路线: eventfd + epoll 共享内存读写 测试程序 取得基准数据, uintr_wait() 比较同样情境下 数据会不会更好一些, 确认后将其拓展为 epoll 可调用形式.

进展

- eventfd + epoll 的测试程序正常,有固定测试数据,能稳定复现.
- 相同场景下使用 uintr_wait() 一直存在问题.

uintr_wait() 卡死 BUG:

- 表现: uintr_wait() 通知 共享内存读写, 读进程可能会再无输出.

- 排查
  - 这个 bug 卡了很久,最终确认是 rfc_v1 的 bug, 一个后续的 [commit](https://github.com/intel/uintr-linux-kernel/commit/b855959c2fe314fca477b19b244acdb876294712) 中有提到 uintr_wait() 有概率导致无法从系统调用返回.

  - 这个 [commit](https://github.com/intel/uintr-linux-kernel/commit/b855959c2fe314fca477b19b244acdb876294712) 给的解决方案是添加一个 100us 的默认超时.

  - 过程分析:
    - 写进程间隔发信号 1s 正常 100ms 正常 10ms 和 0 都会遇到 bug.
    - qemu 中有寄存器比较逻辑错误,但影响的不是这个问题. rfc-v1 代码排查也是一无所获.
    - 重新搜索相关资料,在 rfc-v1 后的分支 [rfc-kernel-to-user-notify-5.18](https://github.com/intel/uintr-linux-kernel/tree/rfc-kernel-to-user-notify-5.18) 的后续 commit 说明中才有提到这个问题.

- 排除:  [rfc-kernel-to-user-notify-5.18](https://github.com/intel/uintr-linux-kernel/tree/rfc-kernel-to-user-notify-5.18) 分支并未给出更具体 fix, 目前在看 poc-v2 的代码 (数据结构变动很多),寻找更多信息.预计 今天或明天上午 会有更明确结论.

## 7.14

rfc_v1 合并 [Add a default timeout option to uintr_wait()](https://github.com/intel/uintr-linux-kernel/commit/b855959c2fe314fca477b19b244acdb876294712)后.uintr_wait 终于是能跑了, 但是速度真的惨..

- 差不多在 2000-2500 之间,与中间几次卡死有关.

将默认的 100us 加到 10000us 后 居然...

```
Message size:       1
Message count:      100000
Total duration:     26657.143   ms
Average duration:   258.230     us
Minimum duration:   38.144      us
Maximum duration:   20887.296   us
Standard deviation: 676.271     us
Message rate:       3751        msg/s
```

换到 5000us

```
============ RESULTS ================
Message size:       1
Message count:      100000
Total duration:     22009.365   ms
Average duration:   212.063     us
Minimum duration:   62.976      us
Maximum duration:   308982.016  us
Standard deviation: 1100.458    us
Message rate:       4543        msg/s
=====================================
```

换到 2500us

```
============ RESULTS ================
Message size:       1
Message count:      100000
Total duration:     21425.744   ms
Average duration:   206.381     us
Minimum duration:   40.192      us
Maximum duration:   365259.520  us
Standard deviation: 1178.572    us
Message rate:       4667        msg/s
=====================================
```

2500 - 2750 us 之间,最好的结果看到过 5200 多.

重新开启 TNT 实验./doge/: 只保留 uintr_wait 唤醒这一条路径,其他掐断.

- 线程唤醒时候 `switch_uintr_return`
  - `UPID_SN` 置位,不再允许接收用户中断请求
  - 不再发送 `send_IPI_self`

- 线程切回后台时 `switch_uintr_prepare`
  - `UPID_SN` 清除,允许接收用户中断请求

- `uintr_wake_up_process`
  - 不再替换 `UINTR_NOTIFICATION_VECTOR` 向量.

待定:

- `uintr_receiver_wait`

29623183 microseconds

6241087 microseconds

3097143 microseconds

### 123

在 rfc_v1 版本应用了  [rfc-kernel-to-user-notify-5.18](https://github.com/intel/uintr-linux-kernel/tree/rfc-kernel-to-user-notify-5.18), 可以跑通 uintr_wait() 了, 但只是规避一下卡死的 bug.

其实 intel 有给出 uintr 在 block 情况下的通知效率, youtube 的视频演讲中提到的 block 下的效率提升,和您之前说的一样,来自 发送方节省了一点资源.
昨天重跑的数据中 eventfd 的速率大致是 5506 msg/s, 跑 uintr_block 目前大致是 4000-5000 之间,取决于 uintr_wait() 卡死了几次,然后定时器返回.

- [User Interrupts - A faster way to Signal - Sohil Mehta - YouTube](https://www.youtube.com/watch?v=JfjzEFeDW_A)
- [User_Interrupts_LPC_2021.pdf](https://lpc.events/event/11/contributions/985/attachments/756/1417/User_Interrupts_LPC_2021.pdf)

![](C:\Users\ZY\AppData\Local\Temp\企业微信截图_16893303305053.png)

接下来工作:

- 把之前的 uintr_wait 测试程序整理一下,给出几个具体的结果.
- 能够达到 intel 结果情况下,包装成 epoll 形式, 目前思路大致有2个, 1个是 直接 配合 eventfd ,在内核中断里 发写消息. 2 创建一个字符设备,配合 epoll .

Rust 有关

- 基础开发环境,有了,开始跟着 rust 文档过基础语法.

## 7.17

回到最初的问题: eventfd + epoll 通知机制遇到瓶颈了

限定条件: 唤醒读者进程时候上下文切换太多,速度不够,就想试试 uintr 能不能突破这个瓶颈.

uintr 中断的处理方式对现有代码改造太多,希望能写成 epoll 可以调用形式.

将 uintr 当作 eventfd 用,

## 7.19

eventfd 跑通但是还有 bug

```c
senduipi core: 1 uitte index:0  dist core: 0 ifsend: 1, nv: 233
senduipi for real
intno 233
[ 2523.129492] uintr_event
[ 2523.129847] uintr_event_write
writer i 388
reader i = 387
uitte index:0
uitt addr: 0xffff9ffe80256001  upid addr: 0xffff9ffe81839a40
1puir is 0x1
upid status: 0b000000001
senduipi core: 1 uitte index:0  dist core: 0 ifsend: 0, nv: 233
```

像是这样的死锁, ON 没有置位... epoll_wait 并没有阻塞,而是直接返回了,这样就执行不到线程阻塞时候 ON 的置位了 --> ~~uintr_event_list. 唤醒时候置位~~ 二次使用有 空指针错误 --> epoll_wait 时候通过 current 获取到 upid 执行重置

```c
writer i 1009
uitte index:0
uitt addr: 0xffff9b5ec1a93001  upid addr: 0xffff9b5ec1c10c80
1puir is 0x1
upid status: 0b000000001
senduipi core: 0 uitte index:0  dist core: 1 ifsend: 0, nv: 233
writer i 1010
reader i = 921
[   25.749606] uintr_event_rst uvect=233
uitte index:0
uitt addr: 0xffff9b5ec1a93001  upid addr: 0xffff9b5ec1c10c80
1puir is 0x1
upid status: 0b000000000
senduipi core: 0 uitte index:0  dist core: 1 ifsend: 1, nv: 233
senduipi for real
intno 233
[   25.750483] uintr_event
[   25.750794] uintr_event_write
writer i 1011
reader i = 922
[   25.751467] uintr_event_rst uvect=233
uitte index:0
uitt addr: 0x0  upid addr: 0x0
1puir is 0x1
upid status: 0b000000000
senduipi core: 1 uitte index:0  dist core: 0 ifsend: 1, nv: 0
senduipi for real
writer i 1012
uitte index:0
uitt addr: 0x0  upid addr: 0x0
1puir is 0x1
upid status: 0b000000000
```

又遇到了 nv 重置为 0 的问题. 问题出在了 `uitt addr: 0x0  upid addr: 0x0`.

出问题前 `senduipi core: 0  dist core: 1` ,出问题时 `senduipi core: 1  dist core: 0` ??

在 qemu 的 `helper_senduipi` 中增加了

```c
    // read tempUITTE from 16 bytes at UITTADDR+ (reg « 4);
    uint64_t uitt_phyaddress = get_hphys2(cs, (env->uintr_tt>>3)<<3 , MMU_DATA_LOAD, NULL);
    qemu_log("qemu: uitt_phyaddress %lx \n", uitt_phyaddress);
    struct uintr_uitt_entry uitte;
    cpu_physical_memory_rw(uitt_phyaddress + (uitte_index<<4), &uitte, 16,false);
    qemu_log("qemu: data of uitt valid:%d user_vec:%d \n",uitte.valid, uitte.user_vec);
    qemu_log("qemu: UPID address 0x%016lx\n", uitte.target_upid_addr);
```

```c
 */
void switch_uintr_return(void)
{
	struct uintr_upid *upid;
	u64 misc_msr;

	if (is_uintr_sender(current)){
		printk("uintr sender return\n");
		printk("uintr_tt is %lx\n",(u64)current->thread.ui_send->uitt_ctx->uitt | 1);
		// wrmsrl(MSR_IA32_UINTR_TT, (u64)current->thread.ui_send->uitt_ctx->uitt | 1);
		return;
	}
```

log

```c
senduipi for real
[   19.342417] uintr sender return
[   19.342567] uintr_tt is ffff8b7501d31001
writer i 338
[   19.342820] uintr sender return
[   19.342820] uintr_tt is ffff8b7501d31001
[   19.343347] uintr sender return
[   19.343540] uintr_tt is ffff8b7501d31001
[   19.344934] uintr sender return
[   19.345115] uintr_tt is ffff8b7501d31001
uitte index:0
qemu: uitt_phyaddress 7f7cf8809190
qemu: data of uitt valid:0 user_vec:0
qemu: UPID address 0x0000000000000000
uitt addr: 0x0  upid addr: 0x0
```

冒险一手...居然通过了... 内核的 `void switch_uintr_return(void)`

```c
	u64 misc_msr, uintr_tt_msr,uintr_tmp;

	if (is_uintr_sender(current)){
		// printk("uintr sender return\n");
		// printk("uintr_tt is %lx\n",(u64)current->thread.ui_send->uitt_ctx->uitt | 1);

		uintr_tmp = (u64)current->thread.ui_send->uitt_ctx->uitt | 1;
		rdmsrl(MSR_IA32_UINTR_TT, uintr_tt_msr);
		if (uintr_tt_msr != uintr_tmp)
			wrmsrl(MSR_IA32_UINTR_TT, uintr_tmp);
	}
```

- 每次进程切换到前台之前检查 MSR_IA32_UINTR_TT 值,不对就重写...

到这里似乎测试程序正常了,能跑过,没有再报错...要开始修改测试程序了...

### 性能比较

都是 sm eventfd epoll

读者 共享内存 写入标记 -> 写者读到标记, 发通知 -> 读者唤醒收到通知 -> 结束

## 721

误删了 应用层文件....

内核在 debian 启动遇到问题,正在重置.

应该是最近对 内核修改导致的, 回退到 uintr_wait_fix 是正常的.

add uintr_event_vector 正常

第二个提交不完全.. 编译不通过..

第三个提交 temp3 测试通过

破案... if 判断没小括号 越界了

```c
	if (is_uintr_receiver(tsk)){
		upid_ctx = tsk->thread.ui_recv->upid_ctx;
		upid_ctx->upid->nc.nv = UINTR_EVENT_VECTOR;
		clear_bit(UPID_SN, (unsigned long *)&upid_ctx->upid->nc.status);
		clear_bit(UPID_ON, (unsigned long *)&upid_ctx->upid->nc.status);
		printk("uintr_event_rst uvect=%d\n", upid_ctx->upid->nc.nv);
	}
```

## 测试文件

```c
gcc-11 -O2 -Wall -static -muintr  -mgeneral-regs-only -minline-all-stringops -msse -std=c11 ./uintr-shm-eventfd-bi.c   -lpthread -o ./t 
```

`-mgeneral-regs-only` 无法使用浮点数,会导致

```c
uintr-shm-eventfd-bi.c:(.text+0x1b0): undefined reference to `__floatundidf'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x1c1): undefined reference to `__floatundidf'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x213): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x23a): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x259): undefined reference to `__floatundidf'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x265): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x284): undefined reference to `__floatundidf'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x290): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x2ae): undefined reference to `__floatsidf'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x2c3): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x2d0): undefined reference to `__divdf3'
/usr/bin/ld: uintr-shm-eventfd-bi.c:(.text+0x2d5): undefined reference to `__fixdfsi'
```

---

7.21 周报:

- 本周大部分时间花费在了修 qemu 和 rfc_v1 的问题上, 基本修完了, uintr + eventfd 的例程 能正常 被 epoll 调用,唤醒读者进程.
- 参考 ipc-benchmark, 将测试程序改写, qemu 运行结果如下

```
============ RESULTS ================
Message size:       1
Message count:      100000
Total duration:     5068.356    ms
Average duration:   45.680      us
Minimum duration:   17.670      us
Maximum duration:   139793.230  us
Message rate:       19730       msg/s
=====================================
```

uintr-eventfd 测试程序还有一个同步问题需要解决:

```c
flag_notify(flag);
int num_events = epoll_wait(epollfd, events, 5, -1);
```

- 共享内存的1个 bit 作为标志位, 写进程 自旋等待 读进程置位( `flag_notify` ) 后再发送通知.
- 有可能 读进程执行 `epoll_wait` 之前,写进程就已经发送了通知.
  - 这个顺序对 eventfd 不是问题, 事件会顺利到达读者进程,但对 uintr-eventfd 并不是这样.
  - 写者进程 发送 uintr 通知,硬件会将 ON 置位, 若是 正常用户中断 handler , 硬件清除 ON. 我们这里没有进到 handler,只能软件清除 ON (在 `epoll_wait` 中).
- uintr-eventfd 读进程执行 `epoll_wait` 之前, ON 是置位状态, 写进程就发送通知, 硬件上是发不出去的....

这个同步问题还得想想.. 或许需要换一种方式测量从 发送到唤醒接收方的延迟.

- 读者置位了通知标志位, 写者 等待延迟,再发通知, 并将 start 时间 写入共享内存,读者唤醒读取共享内存的开始时间,作为统计时间..

---

另一种思路:

- 如
- 完全以中断方式 处理消息,



## 7.24

似乎还是不行, 即使写者采用延迟的方式,还是有概率 同步失配. --> 先加一个等待队列, writte 以后直接清零.

因为系统调度的问题,不能假定 读者 写者进程 完全同步...

把  `flag_clear(flag);` 移动到  `flag_wait(flag);` 之后马上执行,似乎解决了同步问题.

虚拟字符设备的方案不靠谱, 字符设备支持 epoll 还是靠 eventfd ;

eventfd 基准测试时候有失败问题, 跳过失败的记录.

```c
gcc-11 -O2 -Wall -static -muintr -minline-all-stringops -std=c11 ./shm-eventfd-bi.c  -lpthread -o ./shm-eventfd-bi -lm

gcc-11 -O2 -Wall -static -muintr -minline-all-stringops -std=c11 ./shm-uintr-event-bi.c -lpthread -o ./shm-uintr-event-bi -lm
```

这里与 ipc-bench 的计算方式不同,因为要和 block 一起比较,这里所有计数和总时间只计量了写者到读者的时间

比较 uintr-event  和 eventfd 在相同场景下的性能

- 共享内存 epoll 等

取时间,多次复测 重启 qemu; 保证编译命令相同;

- 读者进程 epoll_wait 前后取
- 写者进程 write 前写入共享内存, 读者唤醒后取.
  - 单独测量了 读者挂起,从写者 write 到 读者唤醒,这个场景下时间..

### 读者 epoll_wait 前后时间

目前测试程序有个 bug, uintr-event 下,写者进程必须带个 `printf()` 或延迟才行, 思考很久,没有定位到原因... 两个都带 printf下测试数据

eventfd 平均 342us  uintr-event 290us 快了 15%-20%.

### 写者 write -> 读者唤醒

两者差距不大

- 平均 eventfd  89us  uintr-event 86us 误差范围内
- 最大延迟  eventfd 2-3ms  uintr-event 2-3ms

#### 读者阻塞: 写者 write -> 读者唤醒

这里数据有点离谱,我不确定到底原因是什么, 还需要您过一下看看 测试程序...

多次测试, 重启 qemu 等.

平均: evenfd 500-600us  uintr-event 195-250us
最大延迟: evenfd 10-14ms  uintr-event 4-5ms

### 读者进程时间

```c
============ RESULTS ================
Message size:       1
Message count:      99976
Total duration:     1971.766    ms
Average duration:   19.717      us
Minimum duration:   15.520      us
Maximum duration:   2087.090    us
Standard deviation: 28.566      us
Message rate:       50715       msg/s
=====================================
```

eventfd 可以循环不加 `print` uintr-event 就不行....这...😒

event

```c
============ RESULTS ================
Message size:       1
Message count:      49564
Total duration:     16964.216   ms
Average duration:   342.268     us
Minimum duration:   18.170      us
Maximum duration:   33995.240   us
Standard deviation: 897.081     us
Message rate:       2921        msg/s
=====================================
```

uintr

```c
============ RESULTS ================
Message size:       1
Message count:      49842
Total duration:     14456.537   ms
Average duration:   290.047     us
Minimum duration:   30.870      us
Maximum duration:   22947.490   us
Standard deviation: 523.283     us
Message rate:       3447        msg/s
=====================================
```

### 共享内存写入时间

eventfd

```c
============ RESULTS ================
Message size:       1
Message count:      9944
Total duration:     811.748     ms
Average duration:   81.631      us
Minimum duration:   19.650      us
Maximum duration:   36498.100   us
Standard deviation: 410.267     us
Message rate:       12250       msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      99548
Total duration:     8944.639    ms
Average duration:   89.852      us
Minimum duration:   19.830      us
Maximum duration:   24044.070   us
Standard deviation: 300.694     us
Message rate:       11129       msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      99469
Total duration:     8846.706    ms
Average duration:   88.939      us
Minimum duration:   18.410      us
Maximum duration:   21080.950   us
Standard deviation: 327.067     us
Message rate:       11243       msg/s
=====================================
```

uintr

```c
============ RESULTS ================
Message size:       1
Message count:      99999
Total duration:     8651.057    ms
Average duration:   86.511      us
Minimum duration:   33.060      us
Maximum duration:   4660.040    us
Standard deviation: 55.562      us
Message rate:       11559       msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      99999
Total duration:     8673.567    ms
Average duration:   86.736      us
Minimum duration:   34.050      us
Maximum duration:   2381.830    us
Standard deviation: 51.389      us
Message rate:       11529       msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      99999
Total duration:     8431.547    ms
Average duration:   84.316      us
Minimum duration:   32.750      us
Maximum duration:   3903.230    us
Standard deviation: 51.196      us
Message rate:       11860       msg/s
=====================================
```

#### 纯阻塞状态下

qemu 速度不是很稳定, 需要一次开机跑完.

```c
============ RESULTS ================
Message size:       1
Message count:      957
Total duration:     560.862     ms
Average duration:   586.062     us
Minimum duration:   128.060     us
Maximum duration:   14124.200   us
Standard deviation: 1013.985    us
Message rate:       1706        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      976
Total duration:     582.119     ms
Average duration:   596.433     us
Minimum duration:   106.190     us
Maximum duration:   12960.290   us
Standard deviation: 1009.972    us
Message rate:       1676        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      956
Total duration:     577.163     ms
Average duration:   603.727     us
Minimum duration:   126.950     us
Maximum duration:   14238.060   us
Standard deviation: 1103.557    us
Message rate:       1656        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      952
Total duration:     559.785     ms
Average duration:   588.009     us
Minimum duration:   119.640     us
Maximum duration:   13006.670   us
Standard deviation: 973.493     us
Message rate:       1700        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      979
Total duration:     521.035     ms
Average duration:   532.211     us
Minimum duration:   112.940     us
Maximum duration:   11683.860   us
Standard deviation: 855.838     us
Message rate:       1878        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      974
Total duration:     583.920     ms
Average duration:   599.506     us
Minimum duration:   125.590     us
Maximum duration:   17021.070   us
Standard deviation: 1222.474    us
Message rate:       1668        msg/s
=====================================
```

uintr

```c
============ RESULTS ================
Message size:       1
Message count:      5000
Total duration:     507469      ms
Average duration:   607 us
Minimum duration:   135 us
Maximum duration:   12606       us
Message rate:       9   msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      10000
Total duration:     1014258     ms
Average duration:   510 us
Minimum duration:   108 us
Maximum duration:   16608       us
Message rate:       9   msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
write tt 0  core:1
Message count:      10000
Total duration:     4053.797    ms
Average duration:   405.379     us
Minimum duration:   108.950     us
Maximum duration:   16229.030   us
Standard deviation: 399.609     us
Message rate:       2466        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      999
Total duration:     229.277     ms
Average duration:   229.506     us
Minimum duration:   102.350     us
Maximum duration:   3965.010    us
Standard deviation: 176.772     us
Message rate:       4357        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      999
Total duration:     242.659     ms
Average duration:   242.902     us
Minimum duration:   105.860     us
Maximum duration:   3198.650    us
Standard deviation: 167.422     us
Message rate:       4116        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      999
Total duration:     205.432     ms
Average duration:   205.637     us
Minimum duration:   93.270      us
Maximum duration:   4039.710    us
Standard deviation: 187.786     us
Message rate:       4862        msg/s
=====================================
```

```c
============ RESULTS ================
Message size:       1
Message count:      999
Total duration:     195.239     ms
Average duration:   195.434     us
Minimum duration:   98.190      us
Maximum duration:   1749.900    us
Standard deviation: 122.027     us
Message rate:       5116        msg/s
=====================================
```