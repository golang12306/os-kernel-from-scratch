# 内存分配演示

## 目录

- `brk.c` — 演示 brk/sbrk 系统调用（堆扩展）
- `mmap.c` — 演示 mmap 匿名映射

## 编译运行

```bash
gcc brk.c -o brk && ./brk
gcc mmap.c -o mmap && ./mmap
```

## 关键点

- **brk/sbrk**：设置进程数据段末尾地址，堆向上增长
- **mmap**：映射任意地址范围，匿名映射用于分配内存
- 大块内存（>128KB）直接走 mmap，小块走 brk
