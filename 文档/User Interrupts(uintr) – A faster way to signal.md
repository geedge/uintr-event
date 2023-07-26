[User Interrupts - A faster way to Signal](https://lpc.events/event/11/contributions/985/)

- Sohil Mehta
- Linux Plumbers Conference 2021
- [User Interrupts - A faster way to Signal - Sohil Mehta - YouTube](https://www.youtube.com/watch?v=JfjzEFeDW_A)
- [User_Interrupts_LPC_2021.pdf](https://lpc.events/event/11/contributions/985/attachments/756/1417/User_Interrupts_LPC_2021.pdf)

资源

- [RFC PATCH 00/13 x86 User Interrupts support](https://lore.kernel.org/lkml/20210913200132.3396598-1-sohil.mehta@intel.com/)

概述:

- 避开内核传递中断,大幅度提高性能.
- 用户中断来源: 另一个用户态程序; 内核; 外设;
- 异步 单向 ~~非重入~~
- 第一个硬件实现: [Intel Sapphire Rapids](https://en.wikipedia.org/wiki/Sapphire_Rapids)

实现

- 实现: [uintr-linux-kernel](https://github.com/intel/uintr-linux-kernel)
  - 用户到用户
    - [rfc-v1](https://github.com/intel/uintr-linux-kernel/tree/rfc-v1)  linux 5.14.0
    - [poc-v2 ](https://github.com/intel/uintr-linux-kernel/tree/poc-v2) linux 6.0  最后  [commits](https://github.com/intel/uintr-linux-kernel/commits/poc-v2) Oct 14, 2022
  - 内核到用户 [rfc-kernel-to-user-notify-5.18](https://github.com/intel/uintr-linux-kernel/tree/rfc-kernel-to-user-notify-5.18) linux 5.18.0
    - 最后 commit  Jul 19, 2022
- 测试: [uintr-ipc-bench](https://github.com/intel/uintr-ipc-bench)
  - [linux-rfc-v1](https://github.com/intel/uintr-ipc-bench/tree/linux-rfc-v1)
  - [linux-poc-v2](https://github.com/intel/uintr-ipc-bench/tree/linux-poc-v2)
  - [wait-in-kernel](https://github.com/intel/uintr-ipc-bench/tree/wait-in-kernel)

实验数据 rfc-v1 ^b44fd5

| IPC type | Relative Latency |
| -------- | ---------------- |
| User IPI | 1.0              |
| Signal   | 14.8             |
| Eventfd  | 9.7              |
| Pipe     | 16.3             |
| Domain   | 17.3             |

- Updated benchmark: [linux-rfc-v1](https://github.com/intel/uintr-ipc-bench/tree/linux-rfc-v1)

清华大学操作系统大赛

![[Pasted image 20230518142751.png]]

## rfc-v1

### 编译 & 测试

RFC 的  [mbox.gz](https://lore.kernel.org/lkml/20210913200132.3396598-1-sohil.mehta@intel.com/t.mbox.gz)

b4 下载 rfc 并应用

- [下载说明](https://web.archive.org/web/20230512091605/https://blog.xzr.moe/archives/293/)
- b4 切记 apt 安装

```sh
b4 am https://lore.kernel.org/lkml/20210913200132.3396598-1-sohil.mehta@intel.com/
```

- 这样下载的可以用了, 即使 `b4 am -m` 也只能得到一样的内容,
- 应该可以  `git am` 应用了,

```sh
git checkout -b 20210913_sohil_mehta_intel_com 6880fa6c56601bb8ed59df6c30fd390cc5f6dd8f
git am ./20210913_sohil_mehta_x86_user_interrupts_support.mbx
```

- 网络问题...

```sh
# 先 depth=1 再说其他的 commit
git clone --depth=1 https://github.com/torvalds/linux.git
# 5.14.0 还是比较新的 commit 这个级别刚好有需要的 commit
git fetch --depth=100
```

- <https://github.com/torvalds/linux>

```bash
# 保存 config 要启用 CONFIG_X86_USER_INTERRUPTS,但已经默认勾选了.
make menuconfig
make -j9 V=1
```

#### benchmark

```bash
sudo apt-get install pkg-config libzmq3-dev cmake
```

```bash
mkdir build
cd build
cmake ..
make
```

生成测试程序在 `/build/sourse/` 下

- -c 收发多少条目 -s 单个消息大小
- [[#^b44fd5|rfc-v1]] 的配置 Config: 1M ping-pong IPC notifications with message size=1
  - c = 1000000 - s = 1

### 软件

中断传递机制, 中断首先发布到内存位置, 然后在运行时 CPL=3 时直接传递到接收方.

#### API

注册/注销中断处理程序

- `int uintr_register_handler(handler_func, flags);` 注册
- `int uintr_unregister_handler(flags);` 注销

`uintr_fd = uintr_create_fd(vector, flags);` 创建 向量优先级的文件描述符. 向量号(0-63)

对 uintr_fd 访问权限的任何 发送方 都能传递事件

> 发送方是内核/设备  rfc 可传递给相关内核实体再建立连接,但细节尚不明确.

- `uipi_handle = uintr_register_sender(uintr_fd,flags);
- `int uintr_unregister_sender(uintr_fd, flags);`

发送 uintr `void _senduipi(uipi_handle)`

- 接收方正在运行 (CPL=3) 直接交付; 阻塞后恢复也能再交付.

通过 fd 文件描述符传递

### kernel

`UPID` : User Posted Interrupt Descriptor  用户中断描述符 描述了中断的信息: 中断的接收cpu vector (一个中断接收方以有多个中断号接收)

`UITT`: User Interrupt Target Table  中断索引到中断接收方的映射表, 建立中断发送方和中断接收方的连接, 使得中断可以被投递.

接收方-发送方连接的最大数量 1.打开文件描述符的最大数量 2. UITT 大小的限制,目前 4kb 固定. uv 数量目前限定在 64 个.

### 指令

`senduipi <index>` - 向目标任务发送一个 uintr 基于 UITT 索引

`clui`  通过清除 UIF（用户中断标志）来屏蔽用户中断。

`stui`  通过设置 UIF 来取消屏蔽用户中断。

`testui`  测试 UIF 的当前值。

`uiret`  从用户中断处理程序返回。

### 对比

信号

|          | 信号       | uintr   |
| -------- | -------- | ------- |
| 方向       | 单向       | 单向      |
| 目标       | 进程或线程    | 线程      |
| 传递多个中断顺序 | 不确定      | 向量号从高到低 |
| 反馈       | 0-1      | 无       |
| 重入       | 任何信号都能重入 | 无       |

eventfd()

|        | eventfd          | uintr           |
| ------ | ---------------- | --------------- |
| object | Counter - uint64 | Receiver vector |
| 发送     | write            | SENDUIPI        |
| 接受     | read             | 处理程序与向量相关联      |
| 方向     | 双向               | 单向              |
| 传输数据   | 计数               | 无               |

### FAQ

意外收到用户中断?

- 没有先内核注册的任务 不应该或无法接收用户中断

影响内核调度?

- 否, 阻塞时不会接收用户中断, 用户中断不以任何方式改变任务调度

发送方是否知道中断已达到?

- 否,请考虑其他的反馈机制例如 向原发送方发送用户中断 or 共享内存计数

为什么没有中断反馈?

- 发送方-内存-接收方, 内存-接收方可能立刻也可能永远不会发生,这个过程使得硬件很难提供可靠的反馈.

用户中断可以嵌套吗?

- 可以, 中断处理程序可以 `stui`  取消屏蔽用户中断,随后再次发送用户中断
- **但是** 没有优先级控制机制

一个任务接收所有待处理用户中断?

- 硬件只允许一次处理一个向量, 对这个问题解答是 考虑添加一个新系统调用,获取所有待处理中断信息.

进程需要绑定CPU吗?

- 不需要, 任务调度时, UPID 自动更新.

为什么UPID和UITT由内核分配?

- 防止应用程序滥用,防止写入未授权内存.

## 参考资料

> [Intel Posts Initial Code For x86 User Interrupts On Linux - Shows Great Performance Potential - Phoronix](https://www.phoronix.com/news/Linux-RFC-x86-User-Interrupts)
> [新CPU特性 - User space interrupt - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/414262726)
> [architecture-instruction-set-extensions-programming-reference.pdf](https://www.intel.com/content/dam/develop/external/us/en/documents/architecture-instruction-set-extensions-programming-reference.pdf)
> [清华大学-操作系统大赛-Zhidong Wang / project788067 · GitLab (eduxiji.net)](https://gitlab.eduxiji.net/werkeytom/project788067-127721)
