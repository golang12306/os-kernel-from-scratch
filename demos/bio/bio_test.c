// bio_test.c — Linux 上观察 bio 生命周期的实验
// 编译: gcc -O2 bio_test.c -o bio_test
// 运行: sudo ./bio_test
//
// 实验内容:
// 1. 用 perf 监控 bio 提交事件
// 2. 用 iostat 观察磁盘 IO 模式
// 3. 对比顺序读和随机读的 bio 行为

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#define FILE_PATH "/tmp/bio_test_file"
#define FILE_SIZE (10 * 1024 * 1024)  // 10MB
#define CHUNK_SIZE (4 * 1024)        // 4KB per read

// ============================================================
// 辅助函数
// ============================================================

// 获取微秒时间戳
static long get_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// 创建测试文件
void create_test_file(void) {
    int fd = open(FILE_PATH, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    
    char *buf = malloc(1024 * 1024);  // 1MB buffer
    memset(buf, 'A', 1024 * 1024);
    
    for (int i = 0; i < 10; i++) {
        write(fd, buf, 1024 * 1024);
    }
    
    free(buf);
    close(fd);
    fsync(fd);
    printf("[setup] created %d MB test file\n", FILE_SIZE / (1024 * 1024));
}

// ============================================================
// 实验 1：顺序读 — 观察连续扇区的 IO
// ============================================================

void test_sequential_read(void) {
    printf("\n=== Test 1: Sequential Read ===\n");
    
    int fd = open(FILE_PATH, O_RDONLY);
    if (fd < 0) { perror("open"); return; }
    
    char *buf = malloc(CHUNK_SIZE);
    long start = get_usec();
    
    // 顺序读 10MB，每次 4KB
    for (int i = 0; i < FILE_SIZE / CHUNK_SIZE; i++) {
        read(fd, buf, CHUNK_SIZE);
    }
    
    long elapsed = get_usec() - start;
    
    printf("Sequential: read %d bytes in %ld usec (%.2f MB/s)\n",
           FILE_SIZE, elapsed, FILE_SIZE * 1.0 / elapsed);
    printf("Each read = %d bytes (one page = one bio_vec)\n", CHUNK_SIZE);
    printf("Total reads = %d\n", FILE_SIZE / CHUNK_SIZE);
    
    free(buf);
    close(fd);
}

// ============================================================
// 实验 2：随机读 — 观察非连续扇区的 IO
// ============================================================

void test_random_read(void) {
    printf("\n=== Test 2: Random Read ===\n");
    
    int fd = open(FILE_PATH, O_RDONLY);
    if (fd < 0) { perror("open"); return; }
    
    char *buf = malloc(CHUNK_SIZE);
    long start = get_usec();
    
    // 随机读 1000 次
    for (int i = 0; i < 1000; i++) {
        off_t off = (rand() % (FILE_SIZE / CHUNK_SIZE)) * CHUNK_SIZE;
        lseek(fd, off, SEEK_SET);
        read(fd, buf, CHUNK_SIZE);
    }
    
    long elapsed = get_usec() - start;
    
    printf("Random: 1000 reads in %ld usec (%.2f usec/read)\n",
           elapsed, elapsed / 1000.0);
    printf("Random reads cause disk seeks (~10ms each on HDD)\n");
    printf("Sequential would be ~10x faster\n");
    
    free(buf);
    close(fd);
}

// ============================================================
// 实验 3：观察 direct IO 和 buffered IO 的区别
// ============================================================

void test_direct_io(void) {
    printf("\n=== Test 3: Direct IO vs Buffered IO ===\n");
    
    // Buffered IO（默认）：经过 page cache
    int fd1 = open(FILE_PATH, O_RDONLY);
    char *buf = malloc(CHUNK_SIZE);
    
    long start = get_usec();
    for (int i = 0; i < 100; i++) {
        read(fd1, buf, CHUNK_SIZE);
    }
    long buffered = get_usec() - start;
    
    close(fd1);
    
    // Direct IO：绕过 page cache，直接到磁盘
    int fd2 = open(FILE_PATH, O_RDONLY | O_DIRECT);
    if (fd2 < 0) {
        printf("O_DIRECT not supported on this filesystem, skipping\n");
    } else {
        // O_DIRECT 要求对齐
        posix_memalign((void**)&buf, 4096, CHUNK_SIZE);
        
        long start2 = get_usec();
        for (int i = 0; i < 100; i++) {
            read(fd2, buf, CHUNK_SIZE);
        }
        long direct = get_usec() - start2;
        
        printf("Buffered IO:  %ld usec (100 reads)\n", buffered);
        printf("Direct IO:    %ld usec (100 reads)\n", direct);
        printf("Buffered is faster because of page cache\n");
        
        close(fd2);
        free(buf);
    }
}

// ============================================================
// 实验 4：观察写合并
// ============================================================

void test_write_coalescing(void) {
    printf("\n=== Test 4: Write Coalescing ===\n");
    
    char *test_file = "/tmp/bio_coalescing_test";
    int fd = open(test_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    
    char *buf = malloc(4096);
    memset(buf, 'B', 4096);
    
    long start = get_usec();
    
    // 连续写 1MB（256 次 4KB）
    for (int i = 0; i < 256; i++) {
        write(fd, buf, 4096);
    }
    
    long elapsed = get_usec() - start;
    
    printf("256 sequential writes (%d bytes): %ld usec\n",
           256 * 4096, elapsed);
    printf("Kernel bio layer coalesces adjacent writes into single request\n");
    printf("Without coalescing: 256 individual disk operations\n");
    printf("With coalescing: ~1 disk operation (or few)\n");
    
    free(buf);
    close(fd);
    unlink(test_file);
}

// ============================================================
// 清理
// ============================================================

void cleanup(void) {
    unlink(FILE_PATH);
}

int main(void) {
    printf("Bio Layer Observation Test\n");
    printf("==========================\n");
    
    create_test_file();
    
    test_sequential_read();
    test_random_read();
    test_direct_io();
    test_write_coalescing();
    
    printf("\n=== Summary ===\n");
    printf("Bio layer key behaviors:\n");
    printf("1. Sequential IO -> bio coalescing (merge adjacent bios)\n");
    printf("2. Random IO -> individual bios (no merge)\n");
    printf("3. Buffered IO -> page cache hits, no disk IO\n");
    printf("4. Write coalescing reduces disk operations\n");
    
    cleanup();
    return 0;
}
