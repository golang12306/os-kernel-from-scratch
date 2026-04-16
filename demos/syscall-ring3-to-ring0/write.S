; write.S — 手动触发一次 Ring3 → Ring0 系统调用
; 编译：nasm -f elf64 write.S -o write.o && ld write.o -o write && ./write
;
; 这段代码演示了 x86_64 Linux 系统调用的完整过程：
; 1. 设置参数到 RDI, RSI, RDX
; 2. 设置系统调用号到 RAX
; 3. syscall 触发 Ring0 切换
; 4. CPU 自动保存返回地址，跳转到内核入口

section .data
msg db 'hello from ring3 -> ring0!', 0x0a   ; 要输出的字符串 + 换行符

section .text
global _start

_start:
    ; write(fd=1, buf=msg, count=28)
    ; x86_64 Linux 系统调用号：__NR_write = 1
    mov rax, 1                ; RAX = 系统调用号 (write=1)
    mov rdi, 1                ; RDI = fd (1 = stdout)
    mov rsi, msg              ; RSI = 缓冲区地址
    mov rdx, 28               ; RDX = 字节数
    syscall                   ; ← 触发 Ring0 切换

    ; 退出程序
    mov rax, 60               ; __NR_exit_group = 60
    xor rdi, rdi              ; status = 0
    syscall