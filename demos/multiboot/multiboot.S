/* Multiboot Header - 必须位于内核前8192字节内
 * GRUB 会搜索这个 magic header 并加载我们到正确地址
 *
 * 编译: as --32 -o multiboot.o multiboot.S
 *       ld -m elf_i386 -Ttext 0x100000 -o multiboot_kernel multiboot.o
 * 或者用 gcc + objcopy:
 *       gcc -m32 -c multiboot.S -o multiboot.o
 *       objcopy -O elf32-i386 -B i386 multiboot.o multiboot_kernel
 */

.section .multiboot
.align 4

/* Multiboot Header - GRUB 认识这些字段 */
.long 0x1BADB002          /* magic: 0x1BADB002 */
.long 0x00010003          /* flags: PF_ALIGN + PF_MEMINFO + PF_GRAPHICS */
.long -(0x1BADB002 + 0x00010003)  /* checksum: 必须等于 -(magic+flags) */

/* 下面这些字段在 flags 开启后才需要 */
.long 0            /* header_addr:   多字节对齐地址 */
.long 0            /* load_addr:     实际加载地址 */
.long 0            /* load_end_addr: 加载结束地址 */
.long 0            /* bss_end_addr:  BSS结束地址 */
.long 0            /* entry_addr:    入口点地址 */

/* 图形模式 (flags bit 2) */
.short 0           /* mode_type:  0=线性, 1=EGA文本 */
.short 800         /* width:      800像素 */
.short 600         /* height:     600像素 */
.short 0           /* depth:      0=贪婪 */

/* 入口点 - 内核真正开始的地方 */
.section .text
.global _start
.type _start, @function

_start:
    /* GRUB 已经把我们加载到内存，建立了符合规范的寄存器状态:
     *   eax = 0x2BADB002 (multiboot magic)
     *   ebx = 指向 boot_info 结构体的指针
     */
    
    /* 设置段寄存器 (平坦模型) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置栈指针 - 栈从 0x90000 向下生长 */
    movl $0x90000, %ebp
    movl %ebp, %esp

    /* 开启保护模式的中断:
     * 保护模式下 GDT 已建立，需要用 lidt 加载中断描述符表
     * 但我们的简单内核暂时不处理中断，所以先 cli
     */
    cli

    /* 开启 A20 地址线 (如果 bootloader 没开的话)
     * 这行在 QEMU/GRUB 环境里通常不需要，但加上更健壮
     */
    inb $0x92, %al
    orb $2, %al
    outb %al, $0x92

    /* 调用 C 入口点 kernel_main(ebx 传入 boot_info) */
    pushl %ebx              /* boot_info 作为第一个参数 */
    call kernel_main

    /* kernel_main 返回后挂起 */
    jmp .

.size _start, . - _start
