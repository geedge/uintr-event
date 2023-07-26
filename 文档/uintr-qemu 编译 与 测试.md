<https://github.com/OS-F-4>

任务: qemu 跑起支持 uintr 的内核, 跑过 ipc-bench

- 编译 [qemu-uintr](https://github.com/OS-F-4/qemu-uintr)
  - 内部镜像 [zhangyang/qemu-uintr](https://git.mesalab.cn/zhangyang/qemu-uintr)

- 编译 [kernel](https://github.com/OS-F-4/uintr-linux-kernel)
  - 内部镜像 [zhangyang/uintr-linux-kernel](https://git.mesalab.cn/zhangyang/uintr-linux-kernel)
  - 目前对应的是 intel 开发分支的 [rfc-v1](https://github.com/intel/uintr-linux-kernel/tree/rfc-v1)
  - linux 5.15.rc1; 最后 [commit](https://github.com/intel/uintr-linux-kernel/commit/f20fa04c4553c6140dc8dc49e55fa282d2360ece) 时间是 Sep 13, 2021;

- 制作最小系统 busybox ubuntu debian

- 编译 [ipc-bench](https://github.com/OS-F-4/ipc-bench.git) (共享文件夹 `test`) 并测试通过

## pve

虚拟机

- ip 地址: 192.168.41.81 user: root password: gedge 网关 192.168.41.1
- 12c12t 16G 256

```sh
export https_proxy="http://192.168.33.27:7890"
export http_proxy="http://192.168.33.27:7890"
```

## 编译

```shell
cd ~/
# 总目录
mkdir qemu_uintr
```

### qemu

只编译 x86_64 节省时间

```shell
# 编译环境准备
sudo apt install libpixman-1-dev libcairo2-dev libpango1.0-dev libjpeg8-dev libgif-dev ninja-build libcap-ng-dev libattr1-dev

# 修改版 qemu
git clone https://github.com/OS-F-4/qemu-uintr.git qemu
cd qemu
mkdir build
cd build
## 只编译 x86_64 | virtfs 启用,方便共享文件
../configure  --enable-debug  --target-list=x86_64-softmmu --enable-virtfs --enable-trace-backends=log
make -j
```

能运行的 qemu 在 `build/x86_64-softmmu/qemu-system-x86_64`

### 编译内核

```shell
# 编译环境
sudo apt-get install git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison
```

```shell
cd ~/qemu_uintr
# 内核源码较大,先 depth=1 保证能下载下来
git clone --depth=1 https://github.com/OS-F-4/uintr-linux-kernel.git
cd uintr-linux-kernel/
git fetch --depth=10
#git fetch --depth=1000
```

```shell
# 使用默认配置文件生成
make O=build x86_64_defconfig
make O=build menuconfig
```

在选择构建参数时:

- 在 `General setup`目录下, 选择`Initial RAM filesystem and RAM disk (initramfs/initrd) support`
- 在 `Device Drivers`目录中 `Block device` -> `RAM block device support` , 并设置`  Default RAM disk size (kbytes) `为65536KB。
- 在 ` Processor type and features  --->  ` 设置 ` User Interrupts (UINTR)  `

---

如果需要共享文件, 确保 9psetup 对应的 config.

```
# Networking support -> [*]Plan 9 -> 全部启用 
# File system -> Network File Systems -> Plan 9
CONFIG_NET_9P=y
CONFIG_NET_9P_VIRTIO=y
CONFIG_NET_9P_DEBUG=y (Optional)
CONFIG_9P_FS=y
CONFIG_9P_FS_POSIX_ACL=y
# Device Drivers -> Virtio drivers -> PCI/* 全部启用
CONFIG_PCI=y
CONFIG_VIRTIO_PCI=y
CONFIG_PCI_HOST_GENERIC=y (only needed for the QEMU Arm 'virt' board)
```

---

随后开始构建:

```shell
make O=build bzImage -j 12 V=1
make O=build modules -j 4
```

内核的镜像文件会在 `uintr-linux-kernel/build/arch/x86_64/boot/bzImage`

### 文件系统

[uintr-event](https://git.mesalab.cn/zhangyang/uintr-event) 下的 `/script` 有一些构建文件系统脚本,但未测试完全,请酌情使用.

#### busybox 最小文件系统

启动最快,常用于调试.  busybox 1.32.0

```shell
cd ~/qemu_uintr
wget https://busybox.net/downloads/busybox-1.32.0.tar.bz2

tar -xjf busybox-1.32.0.tar.bz2
cd busybox-1.32.0
mkdir build 
make O=build menuconfig 
# 在 settings Build Options 中选择 # [*] Build static binary (no sharedd libs) 
cd build 
make -j4 V=1
make install
```

会有一堆的 log 输出

```shell
  ./_install//usr/sbin/ubirmvol -> ../../bin/busybox
  ./_install//usr/sbin/ubirsvol -> ../../bin/busybox
  ./_install//usr/sbin/ubiupdatevol -> ../../bin/busybox
  ./_install//usr/sbin/udhcpd -> ../../bin/busybox
--------------------------------------------------
You will probably need to make your busybox binary
setuid root to ensure all configured applets will
work properly.
-------------------------------------------------- 
```

开始制作最小系统

```bash
cd ~/qemu_uintr
mkdir -pv initramfs/x86_64_busybox
cd initramfs/x86_64_busybox/
# 一些文件夹
mkdir -pv {bin,sbin,etc,proc,sys,usr/{bin,sbin}}
# 拷贝编译生成文件
cp -av ../../busybox-1.32.0/build/_install/* .
```

文件系统需要初始化脚本,复杂下面文本到 init, 置于文件系统根目录

```shell
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
# 挂载共享文件夹 root == test
mount -t 9p -o trans=virtio,version=9p2000.L test /root
mknod -m 666 /dev/ttyS0 c 4 64
echo -e "\nBoot took $(cut -d' ' -f1 /proc/uptime) seconds\n"
setsid cttyhack sh

exec /bin/sh
```

修改文件权限 `chmod u+x init`

打包文件系统

```shell
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs-busybox-x86_64.cpio.gz
```

### ubuntu 最小系统

需要 root 权限.请切换到 root 运行.

安装 debootstrap

```bash
apt-get install -y debootstrap
```

创建下载文件系统

```bash
debootstrap --components=main,universe focal ./rootfs "http://mirrors.tuna.tsinghua.edu.cn/ubuntu"
```

- 指定清华源,其他源请替换.

下面会开启一个新的 shell. 所有修改会保存到文件系统中.

```shell
cd rootfs
chroot .
```

进入 chroot 环境后

```shell
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
apt-get install -y build-essential manpages-dev software-properties-common
add-apt-repository ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y gcc-11 g++-11 cmake pkg-config libzmqpp-dev wget texinfo git
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 60 --slave /usr/bin/g++ g++ /usr/bin/g++-11
apt-get clean
wget https://mirrors.tuna.tsinghua.edu.cn/gnu/binutils/binutils-2.38.tar.bz2
tar -jxf binutils-2.38.tar.bz2
cd binutils-2.38 && ./configure && make -j && make install && cd ..
rm -rf binutils-2.38 binutils-2.38.tar.bz2
```

根目录下 init 文件,别忘记 `chmod u+x init`.

```shell
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
echo -e "\nBoot took $(cut -d' ' -f1 /proc/uptime) seconds\n"

mount -t devtmpfs none /dev
mount -t devpts none /dev/pts
/sbin/mdev -s

exec /sbin/init

# 挂载共享文件夹到 /mnt/test
mkdir -p /mnt/test
mount -t 9p -o trans=virtio,version=9p2000.L test /mnt/test

exec /bin/bash
```

随后 `exit` 退出新的命令行.

```shell
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../ubuntu-x86_64.cpio.gz
```

## 运行

qemu_uintr 根目录, `run.sh`

```shell
#!/bin/bash
#ubuntu=/home/zy/qemu_uintr/ubuntu-x86_64.cpio.gz
box=/home/zy/qemu_uintr/initramfs/initramfs-busybox-x86_64.cpio.gz
busybox=/home/zy/qemu_uintr/busybox.cpio.gz
debian=/home/zy/qemu_uintr/debian.cpio.gz
ubuntu=/home/zy/qemu_uintr/ubuntu.cpio.gz
PORT=2222
QEMU=/home/zy/qemu_uintr/qemu/build/x86_64-softmmu/qemu-system-x86_64
KERNEL=/home/zy/qemu_uintr/uintr-linux-kernel/build/arch/x86_64/boot/bzImage
KERNEL2=/home/zy/lixnux-6.x/build/arch/x86_64/boot/bzImage
KERNEL_nowake=/home/zy/zbak/bzImage
$QEMU -smp 2  \
-machine q35,kernel_irqchip=split \
-m 2048M   -nographic -cpu qemu64  \
-kernel $KERNEL \
-initrd $debian \
-append "root=/dev/ram0 rw rootfstype=ext4 console=ttyS0 init=/linuxrc" \
-net user,hostfwd=tcp::${PORT}-:22 -net nic,model=e1000e \
-serial mon:stdio \
-virtfs local,path=/home/zy/qemu_uintr/test,mount_tag=test,security_model=passthrough,id=test
# 等待gdb连接,调试内核时候使用
# -s -S
```

- 这里挂载了 `/home/zy/qemu_uintr/test` 到 `/mnt/test` 方便调试,不需要请去除.
  - 对 busybox 不生效

## 测试

测试性能数据时请使用 `*_clean` 分支,防止打印干扰测试效果.

### sample.c

位于 kernel 代码的 `tools/uintr/sample` 下, 编译需要 GCC >= 11;

```sh
gcc-11 -Wall -static -muintr -mgeneral-regs-only -minline-all-stringops uipi_sample.c -lpthread -o ./uipi_sample
```

在 qemu 下运行的输出

```log
stui core: 1
Receiver enabled interrupts
write tt ffff92594181e001  core:0
Sending IPI from sender threaduitt addr: 0xffff92594181e001  upid addr: 0xffff9259571d8280
senduipi core: 0 uitte index:0  dist core: 1 ifsend: 1, nv: 236
write tt 0  core:0

receive, cur core:1
perv: 0 | id:1 not in user mode return
[  340.050016] sending self ipi
receive, cur core:1
        -- User Interrupt handler --
XXXuiret 
Success
```

### ipc-bench

编译(可能有编译环境问题,具体看 ubuntu 文件系统那一小节)

```
git clone -b linux-rfc-v1 https://github.com/OS-F-4/ipc-bench.git
cd ipc-bench
mkdir build
cd build
cmake ..
make
```

- ipc-bench - linux-rfc-v1 有可能遇到 zeromq 相关编译不通过.
- 看了一下 commit 代码很老了,可以在 `CMakeLists.txt` 注释掉.

测试: 传递 100000 次 message 长度 1 `-c 100000 -s 1`

- `uintrfd-bi` 两个线程来回发送消息
- `uintrfd-sm-bi` 是两个线程公用同一块内存进行更大的消息传递
- `uitrfd-bi-wait` 使用 uintr_wait 阻塞,不再轮询,但有卡死 bug,
- `uintrfd-uni` 是两个线程一发一收, 单向发送
- `uint-shm` 是两个进程通过共享内存的方式进行更大的消息的传递

| method      | Average(us) | min(us) | max(us)   | rate(msg/s) |
| ----------- | ----------- | ------- | --------- | ----------- |
| eventfd-bi  | 173         | 12.8    | 11077.888 | 5506        |
| mq          | 305.183     | 135.936 | 7111.168  | 3144        |
| shm         | 5.921       | 4.352   | 7196.160  | 99123       |
| signal      | 176.980     | 49.664  | 17964.800 | 5405        |
| uintrfd-bi  | 21.150      | 16.384  | 14299.392 | 37489       |
| uintrfd-uni | 12.353      | 9.216   | 9268.224  | 54293       |
| uintr-shm   | 12.942      | 10.240  | 6948.608  | 52566       |
| domain      | 279.793     | 81.664  | 13543.424 | 3418        |
| fifo        | 307.649     | 60.928  | 15460.608 | 3149        |
| mmap        | 7.092       | 5.120   | 1254.912  | 79222       |
| pipe        | 285.417     | 64.512  | 20411.136 | 3379        |

- uintr 全部 和 eventfd-bi 都在 busybox 下测试,其他于 debian 12 下测试.
- uintr 全部 max(us) 非常高的原因, qemu 的报错
  - `perv: 0 | id:0 not in user mode return`
  - `do not go to handler`

## poc_v2(kernel 6.1)

源码在 <https://github.com/intel/uintr-linux-kernel/tree/poc-v2>

已有 qemu 要运行,还需要基于这个 [commit](https://github.com/OS-F-4/uintr-linux-kernel/commit/9f6a41b01ad529801838f1bd8175ed76c1509221) 修改 kernel 代码

- poc_v2 是使用 xsave 操作寄存器, 而 qemu 的实现的是 rfc_v1 的 wrmsrl.

### 编译内核

```shell
# 编译环境
sudo apt-get install git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison
```

```shell
cd ~/qemu_uintr
# 内核源码较大,先 depth=1 保证能下载下来
git clone --depth=1 https://github.com/intel/uintr-linux-kernel.git uintr-linux-kernel-6
cd uintr-linux-kernel-6/
git fetch --depth=10
#git fetch --depth=1000
```

```shell
# 使用默认配置文件生成
make O=build x86_64_defconfig
make O=build menuconfig
```

在选择构建参数时:

- 在`General setup`目录下, 选择`Initial RAM filesystem and RAM disk (initramfs/initrd) support`。
- 在`Device Drivers`目录中`Block device` -> `RAM block device support` , 并设置`  Default RAM disk size (kbytes) `为65536KB。
- 在` Processor type and features  --->  `设置`  User Interrupts (UINTR) ` 和 `User Interrupts - blocking supprot`.

随后开始构建:

```shell
make O=build bzImage -j 12 V=1
make O=build modules -j 4
```

内核的镜像文件会在 `uintr-linux-kernel/build/arch/x86_64/boot/bzImage`

最小文件系统构建等与 5.x 相同.

### 测试

poc_v2 的测试程序在 `tools/uintr/manpages/0_overview.txt` 中,并没有给出单独的文件

编译方式同上,运行成功结果同上.

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <unistd.h>
#include <x86gprintrin.h>

#define __NR_uintr_register_handler     471
#define __NR_uintr_unregister_handler   472
#define __NR_uintr_vector_fd       473
#define __NR_uintr_register_sender 474
#define __NR_uintr_unregister_sender    475

#define uintr_register_handler(handler, flags)    syscall(__NR_uintr_register_handler, handler, flags)
#define uintr_unregister_handler(flags)      syscall(__NR_uintr_unregister_handler, flags)
#define uintr_vector_fd(vector, flags)       syscall(__NR_uintr_vector_fd, vector, flags)
#define uintr_register_sender(fd, flags)     syscall(__NR_uintr_register_sender, fd, flags)
#define uintr_unregister_sender(ipi_idx, flags)   syscall(__NR_uintr_unregister_sender, ipi_idx, flags)

unsigned int uintr_received;
unsigned int uvec_fd;

void __attribute__ ((interrupt)) uintr_handler(struct __uintr_frame *ui_frame,
                            unsigned long long vector)
{
    static const char print[] = "\t-- User Interrupt handler --\n";

    write(STDOUT_FILENO, print, sizeof(print) - 1);
    uintr_received = 1;
}

void *sender_thread(void *arg)
{
    int uipi_index;

    uipi_index = uintr_register_sender(uvec_fd, 0);
    if (uipi_index < 0) {
            printf("Sender register error\n");
            exit(EXIT_FAILURE);
    }

    printf("Sending IPI from sender thread\n");
    _senduipi(uipi_index);

    uintr_unregister_sender(uipi_index, 0);

    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t pt;

    if (uintr_register_handler(uintr_handler, 0)) {
            printf("Interrupt handler register error\n");
            exit(EXIT_FAILURE);
    }

    uvec_fd = uintr_vector_fd(0, 0);
    if (uvec_fd < 0) {
            printf("Interrupt vector registration error\n");
            exit(EXIT_FAILURE);
    }

    _stui();
    printf("Receiver enabled interrupts\n");

    if (pthread_create(&pt, NULL, &sender_thread, NULL)) {
            printf("Error creating sender thread\n");
            exit(EXIT_FAILURE);
    }

    /* Do some other work */
    while (!uintr_received)
            usleep(1);

    pthread_join(pt, NULL);
    close(uvec_fd);
    uintr_unregister_handler(0);

    printf("Success\n");
    exit(EXIT_SUCCESS);
}
```
