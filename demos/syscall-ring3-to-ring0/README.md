# 系统调用：Ring3 到 Ring0 的完整旅程

## 概述

这篇文章配套的 x86 汇编演示代码。

## 运行

```bash
nasm -f elf64 write.S -o write.o
ld write.o -o write
./write
```

输出：

```
hello from ring3 -> ring0!
```

## 关键点

- `syscall` 指令触发 Ring3 → Ring0 切换
- 参数通过 RDI, RSI, RDX 传递（x86_64 ABI）
- 系统调用号放在 RAX
- `iretq` 从 Ring0 返回 Ring3