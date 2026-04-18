# mkfifo / mknod 演示

## 文章核心内容

- mkfifo(path, mode) 创建命名管道
- mknod(path, S_IFIFO, 0)
- open O_RDWR 不阻塞
