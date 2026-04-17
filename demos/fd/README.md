# 文件描述符演示

## 文章核心内容

- fd 是进程本地的索引，指向 fd 数组
- fd → file 对象 → inode → 磁盘文件
- fork 继承 fd，共享 file 对象
- dup/dup2 复制 fd
- FD_CLOEXEC 防止 exec 时 fd 泄漏

## 实用命令

```bash
ls /proc/$$/fd/  # 当前进程 fd 列表
lsof -p <pid>    # 进程打开的所有文件
```
