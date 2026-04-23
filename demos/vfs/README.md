# VFS Demo

## 文章对应代码

- `vfs.h` / `vfs.c` — 简化版 VFS 实现（自研 OS 用）
- `ext4_ops.c` — ext4 文件系统如何"插入"VFS

## 源码文件说明

### vfs.h / vfs.c

VFS 核心数据结构 + 统一接口实现：

- `struct inode` — 文件/目录的身份证
- `struct dentry` — 路径组件缓存
- `struct file` — 进程打开的文件对象
- `vfs_open()` / `vfs_read()` / `vfs_write()` — 统一入口，调度具体文件系统

### ext4_ops.c

演示 ext4 如何接入 VFS：

- 实现 `struct file_operations` 和 `struct inode_operations`
- `ext4_mount()` 时把函数指针表挂到 VFS inode 上
- 演示"插件架构"的工作原理

---

## 文章核心内容

- VFS 是什么：Linux 文件系统的"插卡式中间层"
- inode / dentry / file 三个核心数据结构
- 路径查找：VFS 如何调度具体文件系统的 lookup
- 读写流程：VFS 通过函数指针调度 ext4/xfs/btrfs
- 插件架构：每个文件系统实现同一套接口，VFS 统一管理
