/* hello_core.bpf.c — CO-RE 版本：追踪 openat 系统调用
 *
 * 编译：clang -target bpf -O2 -g -c hello_core.bpf.c -o hello_core.bpf.o
 * 需要 vmlinux.h（同目录）和 libbpf headers。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* 挂载到 tracepoint：进入 openat 系统调用时执行 */
SEC("tracepoint/syscalls/sys_enter_openat")
int handle_openat(struct trace_event_raw_sys_enter *ctx)
{
    char filename[256];
    char comm[16];

    /* 从 tracepoint 参数结构体读取文件名（第二个参数是路径指针） */
    bpf_probe_read_user_str(filename, sizeof(filename), (void *)ctx->args[1]);

    /* 获取当前进程名（读取 task_struct.comm） */
    bpf_get_current_comm(&comm, sizeof(comm));

    /* 调试输出到 /sys/kernel/debug/tracing/trace_pipe */
    bpf_printk("openat by %s: %s\n", comm, filename);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
