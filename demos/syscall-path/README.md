# 系统调用完整旅程演示

## 文章核心内容

- glibc 把参数放入 RDI, RSI, RDX
- syscall 指令查 MSR_LSTAR → entry_SYSCALL_64
- do_syscall_64 查 sys_call_table 分发
- iretq 返回用户态
