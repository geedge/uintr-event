#!/bin/bash

set -e
set -x

# 定义变量
DEPENDENCIES="libpixman-1-dev libcairo2-dev libpango1.0-dev libjpeg8-dev libgif-dev ninja-build libcap-ng-dev libattr1-dev"
TARGET="x86_64-softmmu"
CONFIG_OPTIONS="--enable-debug --target-list=$TARGET --enable-virtfs --enable-trace-backends=log"

# 编译环境准备
sudo apt update
sudo apt install $DEPENDENCIES

cd .. # 进入根目录
if [ -e "./qemu" ]; then
    read -r -p "./qemu\" 已存在,是否删除? (回车确认)" confirm
    if [[ $confirm == "" ]]; then
        rm -rf "./qemu"
    else
        exit 1
    fi
fi

# 克隆 QEMU 代码库
git clone https://github.com/OS-F-4/qemu-uintr.git qemu
cd qemu

# 创建 build 目录并进入
mkdir build
cd build

# 配置和编译 QEMU
../configure $CONFIG_OPTIONS
make -j
