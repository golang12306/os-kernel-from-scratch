# BPF 演示

## 文章核心内容

- BPF Verifier：静态分析确保程序安全
- eBPF vs cBPF：2014 年大改版，性能和功能大幅提升
- JIT 编译：BPF bytecode → native code，等速运行
- bcc / bpftrace / libbpf：三种开发框架对比

## 实用命令

```bash
# 检查 BPF JIT 是否开启
cat /proc/sys/net/core/bpf_jit_enable

# 用 bpftrace 追踪 open 系统调用
bpftrace -e 'tracepoint:syscalls:sys_enter_open { printf("%s: %s\n", comm, args->filename); }'

# 用 bcc 的 execsnoop
python3 /usr/share/bcc/tools/execsnoop

# tcpdump 本质是 BPF
tcpdump -i eth0 'tcp port 8080'
```
