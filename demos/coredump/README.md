# coredump 演示

## 文章核心内容

- SIGSEGV 等致命信号触发 core dump
- do_coredump 遍历 VMA，写入 ELF 格式
- 生产环境通常关 core dump（磁盘、安全）
- gdb bt 查看崩溃调用栈

## 常用命令

```bash
ulimit -c unlimited  # 开启
gdb -c core.<pid> ./program  # 分析
bt                  # 调用栈
```
