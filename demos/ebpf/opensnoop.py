#!/usr/bin/env python3
# ebpf/opensnoop.py — 追踪所有 open/openat 调用（bcc 版）
# 用法: sudo python3 opensnoop.py
# 依赖: bpfcc-tools (Ubuntu/Debian) 或 python3-bcc (pip)

from bcc import BPF

program = r"""
#include <uapi/linux/ptrace.h>

// 定义要输出的数据结构
struct data_t {
    u32 pid;       // 进程 ID
    u32 uid;       // 用户 ID
    char comm[16]; // 进程名
    char filename[256]; // 打开的文件名
};

// 使用 BPF_PERF_OUTPUT 将数据发送回用户态
BPF_PERF_OUTPUT(events);

// 跟踪 enter_openat：进入系统调用时执行
// 这里的 "ht" 是 bpftrace 的简写，等价于 tracepoint:syscalls:sys_enter_openat
// bcc 需要用 KPROBE 定义
TRACEPOINT_PROBE(syscalls, sys_enter_openat)
{
    struct data_t data = {};

    data.pid = bpf_get_current_pid_tgid() >> 32;
    data.uid = bpf_get_current_uid_gid() & 0xFFFF;
    bpf_get_current_comm(&data.comm, sizeof(data.comm));

    // 从参数结构体读取文件名（第一个参数是指针）
    // struct pt_regs *ctx 是自动提供的
    bpf_probe_read_user_str(data.filename, sizeof(data.filename),
                            (char *)args->name);

    events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}
"""

b = BPF(text=program)

# 定义输出格式
def print_event(cpu, data, size):
    import ctypes
    event = ctypes.cast(data, ctypes.POINTER(ctypes.Structure)).contents
    print(f"{event.comm.decode():16s} {event.pid:<6d} {event.filename.decode()}")

# 打开 ring buffer，绑定回调
b["events"].open_perf_buffer(print_event)

print(f"{'COMM':<16} {'PID':<6} FILE")
print("-" * 70)

# 轮询事件，按 Ctrl-C 退出
while True:
    b.perf_buffer_poll()
