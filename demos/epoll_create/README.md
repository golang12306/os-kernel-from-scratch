# epoll_create 演示

## 文章核心内容

- epoll_create1(flags) 创建 epoll 实例
- EPOLL_CLOEXEC = exec 时自动关闭
- O(1) 事件通知，替代 poll/select
