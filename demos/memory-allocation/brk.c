# brk.c — 演示 brk 系统调用（堆扩展）
# 编译：gcc brk.c -o brk && ./brk

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>

int main() {
    void *orig_brk = sbrk(0);           // 获取当前 brk 位置
    printf("current brk: %p\n", orig_brk);

    void *new_brk = sbrk(4096);          // 用 sbrk 扩展 4KB（实际调用 brk）
    if (new_brk == (void *)-1) {
        perror("sbrk failed");
        return 1;
    }

    printf("new brk: %p (+4096 bytes)\n", sbrk(0));

    // 在新内存里写点什么，验证分配成功
    char *p = new_brk;
    p[0] = 'H'; p[1] = 'i'; p[2] = '\0';
    printf("write at new brk: %s\n", p);

    return 0;
}
