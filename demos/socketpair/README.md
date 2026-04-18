# socketpair 演示

## 文章核心内容

- socketpair 创建一对已连接 socket
- 全双工通信（双向读写）
- 替代 pipe（单向）
- 支持 SOCK_STREAM 和 SOCK_DGRAM
- 可传递文件描述符（SCM_RIGHTS）
