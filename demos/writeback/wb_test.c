// wb_test.c — Linux writeback 实验
// 演示 dirty page 的产生、writeback 机制、sync/fsync 的作用
// 编译: gcc -O2 wb_test.c -o wb_test
// 运行: sudo ./wb_test

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#define FILE_PATH "/tmp/wb_test_file"
#define PAGE_SIZE 4096
#define NUM_PAGES 1000

// 读取 /proc/meminfo 中的 Dirty 值（KB）
static unsigned long get_dirty_kb(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;
    
    char line[256];
    unsigned long dirty = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "Dirty: %lu kB", &dirty) == 1) {
            break;
        }
    }
    fclose(fp);
    return dirty;
}

// 测试1: 观察 dirty page 积累
void test_dirty_accumulation(void) {
    printf("=== Test 1: Dirty page accumulation ===\n");
    
    int fd = open(FILE_PATH, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    
    char *buf = malloc(PAGE_SIZE * NUM_PAGES);
    memset(buf, 0xAB, PAGE_SIZE * NUM_PAGES);
    
    unsigned long before = get_dirty_kb();
    printf("Before write: Dirty = %lu KB\n", before);
    
    // 写 1000 页（~4MB），只用 write（不进磁盘，进 page cache）
    ssize_t written = write(fd, buf, PAGE_SIZE * NUM_PAGES);
    printf("Wrote %zd bytes (to page cache, not disk)\n", written);
    
    unsigned long after = get_dirty_kb();
    printf("After write:  Dirty = %lu KB (+%lu KB)\n", after, after - before);
    
    // 不用 fsync，数据还在 page cache 里
    printf("Data is in page cache (dirty), NOT on disk yet.\n");
    printf("If power loss now, data is LOST.\n");
    
    free(buf);
    close(fd);
}

// 测试2: fsync 的作用
void test_fsync_latency(void) {
    printf("\n=== Test 2: fsync latency ===\n");
    
    int fd = open(FILE_PATH, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    
    char buf[PAGE_SIZE];
    memset(buf, 0xCD, PAGE_SIZE);
    
    struct timeval start, end;
    
    // 单次 write + fsync
    gettimeofday(&start, NULL);
    write(fd, buf, PAGE_SIZE);
    fsync(fd);
    gettimeofday(&end, NULL);
    
    long usec = (end.tv_sec - start.tv_sec) * 1000000 
              + (end.tv_usec - start.tv_usec);
    
    printf("write + fsync (1 page): %ld usec\n", usec);
    printf("Disk latency is ~10ms for HDD, ~0.1ms for SSD.\n");
    
    close(fd);
}

// 测试3: 对比 write-only vs write+fsync
void test_write_vs_fsync(void) {
    printf("\n=== Test 3: write-only vs write+fsync throughput ===\n");
    
    // 测试1: 纯 write（使用 writeback，异步）
    int fd1 = open("/tmp/wb_nowriteback", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    char *buf = malloc(PAGE_SIZE * 10000);
    memset(buf, 0xEF, PAGE_SIZE * 10000);
    
    struct timeval t1_start, t1_end;
    gettimeofday(&t1_start, NULL);
    write(fd1, buf, PAGE_SIZE * 10000);
    gettimeofday(&t1_end, NULL);
    long write_usec = (t1_end.tv_sec - t1_start.tv_sec) * 1000000 
                    + (t1_end.tv_usec - t1_start.tv_usec);
    
    printf("write 10000 pages (no fsync):  %ld usec (%.2f MB/s)\n",
           write_usec, (PAGE_SIZE * 10000) * 1.0 / write_usec);
    
    free(buf);
    close(fd1);
    
    // 测试2: 每 1000 页 fsync 一次（分批落盘）
    int fd2 = open("/tmp/wb_batch_fsync", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    buf = malloc(PAGE_SIZE * 1000);
    memset(buf, 0x12, PAGE_SIZE * 1000);
    
    struct timeval t2_start, t2_end;
    gettimeofday(&t2_start, NULL);
    
    for (int i = 0; i < 10; i++) {
        write(fd2, buf, PAGE_SIZE * 1000);
        fsync(fd2);  // 每 1000 页落盘一次
    }
    
    gettimeofday(&t2_end, NULL);
    long batch_usec = (t2_end.tv_sec - t2_start.tv_sec) * 1000000 
                    + (t2_end.tv_usec - t2_start.tv_usec);
    
    printf("write 10000 pages (fsync every 1000): %ld usec (%.2f MB/s)\n",
           batch_usec, (PAGE_SIZE * 10000) * 1.0 / batch_usec);
    
    free(buf);
    close(fd2);
    
    printf("\nRatio: write-only is ~%dx faster than write+fsync\n", 
           batch_usec / (write_usec > 0 ? write_usec : 1));
}

// 清理
void cleanup(void) {
    unlink(FILE_PATH);
    unlink("/tmp/wb_nowriteback");
    unlink("/tmp/wb_batch_fsync");
}

int main(void) {
    printf("Writeback observation test\n");
    printf("=========================\n");
    
    test_dirty_accumulation();
    test_fsync_latency();
    test_write_vs_fsync();
    
    printf("\n=== Cleanup ===\n");
    cleanup();
    printf("Done.\n");
    
    return 0;
}
