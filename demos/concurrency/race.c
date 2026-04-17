// race.c — 演示竞争条件（修复后用 mutex）
// 编译：gcc race.c -lpthread -o race && ./race

#include <stdio.h>
#include <pthread.h>

volatile long counter = 0;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

void *worker(void *arg) {
    for (int i = 0; i < 1000000; i++) {
        pthread_mutex_lock(&m);
        counter++;
        pthread_mutex_unlock(&m);
    }
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, NULL);
    pthread_create(&t2, NULL, worker, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("counter = %ld (expected 2000000)\n", counter);
    return 0;
}
