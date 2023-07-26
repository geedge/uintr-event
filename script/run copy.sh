#!/bin/bash
ubuntu=/home/zy/qemu_uintr/ubuntu-x86_64.cpio.gz
box=/home/zy/qemu_uintr/initramfs/initramfs-busybox-x86_64.cpio.gz
debian=/home/zy/qemu_uintr/debian.cpio.gz
PORT=2222
QEMU=/home/zy/qemu_uintr/qemu/build/x86_64-softmmu/qemu-system-x86_64
KERNEL=/home/zy/qemu_uintr/uintr-linux-kernel/build/arch/x86_64/boot/bzImage
$QEMU -smp 2  \
-machine q35,kernel_irqchip=split \
-m 2048M -nographic -cpu qemu64  \
-kernel $KERNEL \
-initrd $debian \
-append "root=/dev/ram0 rw rootfstype=ext4 console=ttyS0 init=/linuxrc" \
-net user,hostfwd=tcp::${PORT}-:22 -net nic,model=e1000e \
-serial mon:stdio
