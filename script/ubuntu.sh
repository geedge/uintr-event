#!/bin/bash

# 该脚本用于在当前目录下创建一个 ubuntu20 系统文件夹，并在其中安装 ssh 服务。
# 如果当前目录下已经存在名为 "ubuntu" 的文件夹，则脚本会退出。
# 运行脚本需要管理员权限。
# 依赖项：debootstrap 工具、apt-get 命令。
# 使用清华大学的 Ubuntu20 镜像源

set -e
set -x

# 进入根目录
cd ..

if [ -e "./ubuntu" ]; then
  read -r -p "./ubuntu\" 已存在,是否删除? (回车确认,其他键取消)" confirm
  if [[ $confirm == "" ]]; then
    rm -rf "./ubuntu"
  else
    exit 1
  fi
fi

# 创建并进入目标文件夹
mkdir ubuntu
cd ubuntu

# 安装 debootstrap 工具
if ! apt-get -qy install debootstrap; then
  echo "无法安装 debootstrap 工具"
  exit 1
fi

# 使用 debootstrap 创建 ubuntu 系统文件
if ! debootstrap --components=main,universe focal ./ "http://mirrors.tuna.tsinghua.edu.cn/ubuntu"; then
  echo "创建 Ubuntu 系统文件时出错"
  exit 1
fi

# 进入新创建的 Ubuntu 系统
if ! chroot . /bin/bash -c "apt-get update"; then
  echo "无法进入新创建的 Ubuntu 系统"
  exit 1
fi

# 进入 chroot 环境, 初始化系统
chroot . /bin/bash <<EOF
set -e

export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
# 安装 ssh 服务
apt-get update
apt-get install -qy openssh-server sudo net-tools bash-completion ssh

# 设置 root 用户的密码
echo "root:root" | chpasswd

# 配置 ssh 登录
# sed -i 's/.*Port.*/Port 22/' /etc/ssh/sshd_config
sed -i 's/^#Port 22/Port 22/' /etc/ssh/sshd_config
# sed -i 's/.*ListenAddress.*/ListenAddress 0.0.0.0/' /etc/ssh/sshd_config
sed -i 's/.*PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

# 重启 ssh 服务
service ssh restart
apt clean
EOF

# 写入 init 文件并设置权限
cp ../script/init_debian init
chroot . /bin/bash -c "chmod u+x /init"

echo "最小化 Ubuntu 系统创建成功！"

if read -r -t 10 -p "打包镜像? (10秒 超时取消)" confirm; then
  find . -print0 | cpio --null -ov --format=newc | gzip -9 >../ubuntu.cpio.gz
  echo "打包完毕"
else
  echo "取消打包"
fi
