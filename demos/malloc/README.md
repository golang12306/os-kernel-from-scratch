# glibc malloc 演示

## 文章核心内容

- glibc arena：预分配大块内存池
- brk：扩展堆给小块分配
- mmap：直接向内核申请大块
- malloc_trim：强制归还 arena 给内核
- tcache：线程本地缓存

## 实用命令

```bash
mallinfo  # 查看 arena 信息
malloc_trim(0)  # 强制 trim
```
