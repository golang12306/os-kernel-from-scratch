# clone 系统调用演示

## 文章核心内容

- fork/vfork/clone 都是 clone 的封装
- CLONE_VM 决定是否共享地址空间
- 线程 = 共享地址空间的 clone
