# mmap.c — 演示 mmap 匿名映射
# 编译：gcc mmap.c -o mmap && ./mmap

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    // 匿名映射：向内核申请 2 个 Page（8KB）
    void *ptr = mmap(
        NULL,                      // NULL = 让内核选地址
        2 * 4096,                  // 8KB
        PROT_READ | PROT_WRITE,    // 可读可写
        MAP_PRIVATE | MAP_ANONYMOUS,  // 私有匿名映射，不关联文件
        -1,                        // 不关联文件
        0
    );

    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    printf("mmap returned: %p\n", ptr);
    printf("page size: %ld bytes\n", sysconf(_SC_PAGESIZE));

    // 写点东西，验证真的分配了物理内存
    char *p = ptr;
    p[0] = 'A'; p[1] = 'B'; p[2] = 'C';
    printf("content: %.3s\n", p);

    // 归还给内核
    munmap(ptr, 2 * 4096);
    return 0;
}
