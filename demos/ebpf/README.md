# eBPF 演示

## 文章关联

- 《从零写OS内核 | eBPF 入门——内核里的「任意门」，想进就进》（241）
- 《从零写OS内核 | eBPF 进阶——CO-RE + libbpf + BTF：一次编译，跨内核运行》（242）

## 文件说明

| 文件 | 说明 |
|------|------|
| `hello.bt` | bpftrace 脚本，追踪所有 open/openat 系统调用（241 配套） |
| `opensnoop.py` | bcc Python 版，追踪所有 open/openat 调用（241 配套） |
| `hello_libbpf.c` | libbpf + CO-RE 版，展示完整 CO-RE 结构（241 配套） |
| `hello_core/` | CO-RE 完整示例：Makefile + .bpf.c + user.c（242 配套） |

## CO-RE 示例（242 配套）

目录：`demos/ebpf/hello_core/`

```makefile
# 生成 vmlinux.h（从本机 BTF 数据）
vmlinux.h:
	sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

# 编译 BPF 目标文件
hello_core.bpf.o: hello_core.bpf.c vmlinux.h
	clang -target bpf -O2 -g -c $< -o $@

# 编译用户态程序
hello_core_user.o: hello_core_user.c
	clang -O2 -Wall -I. -c $< -o $@

all: hello_core.bpf.o hello_core_user.o
	clang hello_core_user.o -o hello_core_user -lbpf -lelf -lz

clean:
	rm -f *.bpf.o *.o hello_core_user vmlinux.h
```

运行：

```bash
# 1. 生成 vmlinux.h（需要 root）
sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

# 2. 编译
make all

# 3. 运行（root）
sudo ./hello_core_user
```

查看 trace 输出：

```bash
# 另一个终端
sudo cat /sys/kernel/debug/tracing/trace_pipe
```
