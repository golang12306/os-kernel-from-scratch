// futex_v1_spinlock.c
// 纯粹的用户态自旋锁：atomic_flag 测试并设置
// 优点：用户态完成，无系统调用
// 缺点：等锁时 CPU 空转，浪费算力

#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

atomic_int lock = 0;  // 0=未占用，1=已占用

void spin_lock(void) {
    // atomic_compare_exchange_strong:
    // 如果 lock==0，则原子地设为 1，返回 true
    // 否则保持 lock==1，返回 false
    // 自旋直到成功
    while (!atomic_compare_exchange_strong(&lock, &(int){0}, 1)) {
        // 空循环，CPU 在这里空转
        // ——这就是问题所在
    }
}

void spin_unlock(void) {
    atomic_store(&lock, 0);
}

long counter = 0;

// 测试
void *worker(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < 1000000; i++) {
        spin_lock();
        counter++;
        spin_unlock();
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

    printf("counter = %ld (expected 2000000)\n", counter);
    return 0;
}
