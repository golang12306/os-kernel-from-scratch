# Makefile for hello_core CO-RE eBPF demo
#
# 依赖：
#   - clang (target bpf support)
#   - bpftool (生成 vmlinux.h)
#   - libbpf-dev
#
# 用法：
#   make vmlinux.h   # 首次需要，生成内核类型头文件
#   make             # 编译所有
#   sudo make run    # 运行（需要 root）

CLANG ?= clang
LLVM_STRIP ?= llvm-strip
CC ?= gcc

.PHONY: all clean vmlinux.h run

# 生成 vmlinux.h（从本机 BTF 数据）
vmlinux.h:
	sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

# 编译 BPF 目标文件（.bpf.o）
hello_core.bpf.o: hello_core.bpf.c vmlinux.h | vmlinux.h
	$(CLANG) -target bpf -O2 -g \
		-Wall -Wunused \
		-I. \
		-c $< -o $@

# 编译用户态程序（native）
hello_core_user.o: hello_core_user.c
	$(CC) -O2 -Wall -I. -c $< -o $@

# 链接用户态程序
hello_core_user: hello_core_user.o hello_core.bpf.o
	$(CC) hello_core_user.o -o $@ -lbpf -lelf -lz

all: vmlinux.h hello_core.bpf.o hello_core_user.o hello_core_user

# 运行（需要 root）
run: hello_core_user
	sudo ./hello_core_user

clean:
	rm -f *.bpf.o *.o hello_core_user vmlinux.h
