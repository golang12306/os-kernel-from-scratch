# VDSO 演示

## 文章核心内容

- VDSO = 内核映射到用户态的代码
- gettimeofday 走 VDSO，不需要 syscall
- vsyscall 是前身，VDSO 是改进
