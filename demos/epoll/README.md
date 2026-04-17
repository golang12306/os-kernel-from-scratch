# IO 多路复用演示

## 文章核心内容

- select/poll：O(n) 每次全遍历
- epoll：O(1) 只返回就绪 fd
- LT（水平触发）：可重复通知
- ET（边缘触发）：变化才通知，必须一次性读完

## 实用命令

```bash
# 查看 epoll 实例数
ls /proc/sys/fs/epoll/
```
