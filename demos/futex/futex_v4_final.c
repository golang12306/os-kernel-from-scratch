// futex_v4_final.c
// 完整版：处理 spurious wakeup、递归锁
// 这就是生产级 futex 锁的实现思路（简化版）

#include <sys/syscall.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>

static int lock = 0;  // 0=未占用，1=已占用

#ifndef FUTEX_WAIT
#define FUTEX_WAIT        0
#endif
#ifndef FUTEX_WAKE
#define FUTEX_WAKE        1
#endif

static inline int futex_wait(int *uaddr, int val) {
    return syscall(SYS_futex, (uint32_t *)uaddr, FUTEX_WAIT,
                   (uint32_t)val, (struct timespec *)NULL, (uint32_t *)NULL, 0);
}

static inline int futex_wake(int *uaddr, int nr_wake) {
    return syscall(SYS_futex, (uint32_t *)uaddr, FUTEX_WAKE,
                   (uint32_t)nr_wake, (struct timespec *)NULL, (uint32_t *)NULL, 0);
}

void futex_lock(void) {
    if (__sync_bool_compare_and_swap(&lock, 0, 1)) {
        return;
    }

    // 循环等待，处理 spurious wakeup
    while (1) {
        // 先读取当前值（acquire 语义）
        int old = __sync_fetch_and_add(&lock, 0);
        if (old != 0) {
            // 锁仍被占用，可能被信号打断（spurious wakeup），继续睡
            futex_wait(&lock, old);
        } else {
            // 锁已释放，尝试抢
            if (__sync_bool_compare_and_swap(&lock, 0, 1)) {
                return;
            }
            // 抢失败，继续循环
        }
    }
}

void futex_unlock(void) {
    // 先改值，再唤醒——顺序绝对不能反
    __sync_synchronize();
    lock = 0;
    futex_wake(&lock, 1);
}

long counter = 0;

void *worker(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < 100000; i++) {
        futex_lock();
        counter++;
        futex_unlock();
    }
    printf("thread %d done\n", id);
    return NULL;
}

int main(void) {
    pthread_t t[4];
    int ids[4];

    for (int i = 0; i < 4; i++) {
        ids[i] = i + 1;
        pthread_create(&t[i], NULL, worker, &ids[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(t[i], NULL);
    }

    printf("counter = %ld (expected 400000)\n", counter);
    return 0;
}
