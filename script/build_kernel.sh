#!/bin/bash

set -e
set -x

# 定义变量
DEPENDENCIES="git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison"
# TARGET="x86_64-softmmu"
# CONFIG_OPTIONS="--enable-debug --target-list=$TARGET --enable-virtfs --enable-trace-backends=log"

sudo apt-get install $DEPENDENCIES

cd ..
# 内核源码较大,先 depth=1 保证能下载下来
git clone --depth=1 https://github.com/OS-F-4/uintr-linux-kernel.git
cd uintr-linux-kernel/
git fetch --depth=10
#git fetch --depth=1000

mkdir build
cp ../../script/.config ./build/

make O=build bzImage -j 12 V=1
make O=build modules -j 4
