# Copy-On-Write 演示

## 文章核心内容

- fork() 只复制页表，不复制物理内存
- 写入时才触发 COW 复制物理页
- fork+exec 高效（不复制大块内存）
