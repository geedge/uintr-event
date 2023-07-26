# uintr-event

## 项目描述

尝试使用 uintr 改进 eventfd+epoll,降低读者写者问题中,读者挂起后唤醒的延迟.

- 基于清华大学 [OS-F-4/usr-intr](https://github.com/OS-F-4/usr-intr)

项目结构

- `文档/` 项目文档
- `资料/` 参考资料
- `kernel6` uintr linux kernel 的 poc_v2 分支代码阅读记录
- `log` 部分排查问题的日志
- `script` 构建 qemu 文件系统部分脚本
- `test` 性能测试代码

相关仓库

- [qemu-uintr](https://git.mesalab.cn/zhangyang/qemu-uintr) qemu 模拟器源码
- [uintr-linux-kernel](https://git.mesalab.cn/zhangyang/uintr-linux-kernel) uintr 的 linux kernel 源码
    - epoll_waitlist 为 uintr-event 修改分支

重要文档

- [[uintr-qemu 编译 与 测试]]
- [[uintr-event]]
- [[uintr 流程梳理]]