/* hello_core_user.c — 用户态加载器：加载并运行 BPF 程序
 *
 * 编译：clang -O2 -Wall -I. -c hello_core_user.c -o hello_core_user.o
 * 链接：clang hello_core_user.o -o hello_core_user -lbpf -lelf -lz
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <errno.h>

int main(int argc, char **argv)
{
    struct bpf_object *obj = NULL;
    struct bpf_program *prog = NULL;
    struct bpf_link *link = NULL;
    int err;

    /* 打开 BPF 对象文件 */
    obj = bpf_object__open("./hello_core.bpf.o");
    err = libbpf_get_error(obj);
    if (err) {
        fprintf(stderr, "failed to open BPF object: %s\n", strerror(-err));
        return 1;
    }

    /* 加载 BPF 程序（libbpf 在这里做 CO-RE relocation） */
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "failed to load BPF object: %s\n", strerror(-err));
        goto cleanup;
    }

    /* 找到目标程序 */
    prog = bpf_object__find_program_by_name(obj, "handle_openat");
    if (!prog) {
        fprintf(stderr, "failed to find BPF program 'handle_openat'\n");
        err = -ENOENT;
        goto cleanup;
    }

    /* 附加到 tracepoint */
    link = bpf_program__attach(prog);
    err = libbpf_get_error(link);
    if (err) {
        fprintf(stderr, "failed to attach BPF program: %s\n", strerror(-err));
        goto cleanup;
    }

    printf("Tracing openat syscalls... Ctrl+C to stop.\n");
    printf("View output with: sudo cat /sys/kernel/debug/tracing/trace_pipe\n");

    /* 保持运行，按 Ctrl-C 退出 */
    while (1) {
        sleep(1);
    }

cleanup:
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return err ? 1 : 0;
}
