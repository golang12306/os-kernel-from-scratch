// futex_v3_futex_lock.c
// 真正的 futex 锁：自旋失败后进内核睡下，被唤醒时重新抢锁
// 用普通 int + __sync_lock_test_and_set / __sync_bool_compare_and_swap
// 避免 atomic 和内核可见性问题

#include <sys/syscall.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>

// 注意：futex 操作需要普通内存地址，不能是 atomic 类型的地址
// 锁值：0 = 未占用，1 = 已占用
static int lock = 0;

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
    // 第一步：尝试在用户态原子地拿到锁
    // __sync_lock_test_and_set 是 release 语义，锁值设回 0 前其他线程看不到
    if (__sync_bool_compare_and_swap(&lock, 0, 1)) {
        return;  // 成功，用户态完成，不进内核
    }

    // 第二步：CAS 失败，锁被占用，进内核睡下
    // 内核会检查 *uaddr == 1，如果已经是 0 则立即返回
    futex_wait(&lock, 1);

    // 第三步：被唤醒（或 spurious wakeup），重新抢锁
    while (!__sync_bool_compare_and_swap(&lock, 0, 1)) {
        futex_wait(&lock, 1);
    }
}

void futex_unlock(void) {
    // 先改值，再唤醒——顺序绝对不能反！
    __sync_synchronize();  // 内存屏障，确保之前的操作都对其他线程可见
    lock = 0;              // 普通 store，release 语义
    futex_wake(&lock, 1);  // 通知内核唤醒等待线程
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
    pthread_t t1, t2;
    int id1 = 1, id2 = 2;

    pthread_create(&t1, NULL, worker, &id1);
    pthread_create(&t2, NULL, worker, &id2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("counter = %ld (expected 200000)\n", counter);
    return 0;
}
