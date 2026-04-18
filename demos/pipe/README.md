# pipe 演示

## 文章核心内容

- pipe[0]=read, pipe[1]=write
- 环形缓冲区 PAGE_SIZE
- fork + pipe + dup2 实现管道命令
