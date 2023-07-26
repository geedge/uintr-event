#!/bin/bash

# 由 busybox 创建一个最小的文件系统 | 不含编译部分
# 创建目录并拷贝文件

set -e
set -x

cd .. || exit

if [ -e "./busybox" ]; then
    read -r -p "./busybox\" 已存在,是否删除? (回车确认,其他键取消)" confirm
    if [[ $confirm == "" ]]; then
        rm -rf "./busybox"
    else
        exit 1
    fi
fi

mkdir -pv busybox
cd busybox || exit
mkdir -pv {bin,sbin,etc,proc,sys,usr/{bin,sbin}}
cp -av ../busybox-1.32.0/build/_install/* .

# 写入 init 文件并设置权限
cp ../script/init_busybox init
chroot . /bin/sh -c "chmod u+x /init"

echo "最小化 Busybox 系统创建成功！"

# 打包文件系统
if read -r -t 10 -p "打包镜像? (10秒 超时取消)" confirm; then
    find . -print0 | cpio --null -ov --format=newc | gzip -9 >../busybox.cpio.gz
    echo "打包完毕"
else
    echo "取消打包"
fi
