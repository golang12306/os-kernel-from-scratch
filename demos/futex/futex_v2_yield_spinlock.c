// futex_v2_yield_spinlock.c
// 改进：自旋一段时间后主动让出 CPU
// 减少 CPU 浪费，但仍无法解决长时间等锁的问题

#include <stdatomic.h>
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

atomic_int lock = 0;

void spin_lock_yield(void) {
    int spin_count = 0;
    while (!atomic_compare_exchange_strong(&lock, &(int){0}, 1)) {
        spin_count++;
        if (spin_count > 1000) {
            // 自旋 1000 次后主动让出 CPU
            sched_yield();  // 告诉调度器：我可以先跑其他线程
            spin_count = 0;
        }
    }
}

void spin_unlock(void) {
    atomic_store(&lock, 0);
}

long counter = 0;

void *worker(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < 1000000; i++) {
        spin_lock_yield();
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
