# sendfile 演示

## 文章核心内容

- 零拷贝：磁盘 → 内核 → 网卡（DMA）
- 传统 read+write = 4 次拷贝
- sendfile = 2 次拷贝
