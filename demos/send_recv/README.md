# send / recv 演示

## 文章核心内容

- send/recv 是 TCP 专用收发函数
- MSG_NOSIGNAL 避免 SIGPIPE
- MSG_DONTWAIT 非阻塞
- MSG_PEEK 偷看数据
- 返回值处理（部分发送、对方关闭）
- TCP 粘包问题
