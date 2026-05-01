# eBPF 演示

## 文章关联

对应公众号文章：《从零写OS内核 | eBPF：内核里的「任意门」，想进就进》

## 文件说明

| 文件 | 说明 |
|------|------|
| `hello.bt` | bpftrace 脚本，追踪所有 open/openat 系统调用 |
| `opensnoop.py` | bcc Python 版，追踪所有 open/openat 调用 |
| `hello_libbpf.c` | libbpf + CO-RE 版，最接近生产的使用方式 |

## 运行方法

### hello.bt（bpftrace）

```bash
# 安装 bpftrace
sudo apt install bpftrace

# 运行
sudo bpftrace demos/ebpf/hello.bt
```

### opensnoop.py（bcc）

```bash
# 安装 bcc-tools
sudo apt install bpfcc-tools linux-headers-$(uname -r)

# 运行（也可以直接用系统自带的）
sudo /usr/share/bcc/tools/opensnoop
# 或
sudo python3 demos/ebpf/opensnoop.py
```

### hello_libbpf.c（libbpf + CO-RE）

需要 clang + libbpf + llvm-objcopy，配合 libbpf-bootstrap 使用。

```bash
# 编译为 BPF 目标文件
clang -target bpf -O2 -g -c demos/ebpf/hello_libbpf.c

# 提取 BPF 字节码
llvm-objcopy -O binary hello_libbpf.o hello_libbpf.bpf.o
```
