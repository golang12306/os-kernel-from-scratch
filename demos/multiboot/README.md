# Multiboot Demo

演示 Multiboot 规范的最小化内核。

## 文件

- `multiboot.S` — Multiboot header + 入口点（32位汇编）
- `main.c` — 内核主代码，在 VGA 文本模式打印 boot_info
- `Makefile` — 交叉编译脚本

## 编译

```bash
as --32 multiboot.S -o multiboot.o
gcc -m32 -fno-pie -c main.c -o main.o
ld -m elf_i386 -Ttext 0x100000 -o kernel.elf multiboot.o main.o
```

## 运行

```bash
# 用 GRUB (真实环境或 ISO)
grub-mkrescue -o kernel.iso kernel.elf
qemu-system-i386 -cdrom kernel.iso

# 或直接用 QEMU 的 -kernel 选项 (不需要 GRUB)
qemu-system-i386 -kernel kernel.elf
```

## 预期输出

QEMU 窗口显示：
```
=== Multiboot Kernel Demo ===
Magic: 0x2BADB002 (OK)
Loader: GRUB 2.x
Cmdline: (内核命令行)
Low mem: 639 KB  High: 262144 KB
Memory map:
  [FREE] 0x0 - 0x9FC00 (0 MB)
  [FREE] 0x100000 - 0x3FF00000 (1023 MB)
Modules: 0
Halted.
```

## 关键点

1. Multiboot header 必须在文件前 8192 字节内，且 4 字节对齐
2. GRUB 会把 magic 放 eax，boot_info 指针放 ebx
3. 内核无需知道加载器细节，只通过标准结构获取信息
