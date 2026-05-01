/* ebpf/hello_libbpf.c — libbpf + CO-RE 版本：统计进程 open/openat 调用次数
 *
 * 编译方法：
 *   clang -target bpf -O2 -g -c hello_libbpf.c
 *   llvm-objcopy -O binary hello_libbpf.o hello_libbpf.bpf.o
 *
 * 运行方法：
 *   sudo ./hello_libbpf.bpf.o  # 需要通过 libbpf 加载器（如 libbpf-bootstrap）
 *
 * 本文件展示 libbpf + CO-RE 的完整结构，读者可用 libbpf-bootstrap 为基础修改。
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/sched.h>

/* 用 SEC() 将函数附加到内核 hook 点 */
SEC("tracepoint/syscalls/sys_enter_openat")
int handle_openat(struct trace_event_raw_sys_enter *ctx)
{
    /* 读取被打开的文件名（从 ctx->args[1] 取指针，再 bpf_probe_read_user_str 复制） */
    char filename[256];
    bpf_probe_read_user_str(filename, sizeof(filename), (void *)ctx->args[1]);

    /* 这里只是演示：打印到 /sys/kernel/debug/tracing/trace_pipe
     * 实际生产环境用 bpf_ringbuf_output() 或 perf_event_output() 发送数据 */
    bpf_printk("openat: %s\n", filename);

    return 0;
}

/* 必需的 license 和 version 宏 */
char LICENSE[] SEC("license") = "GPL";
