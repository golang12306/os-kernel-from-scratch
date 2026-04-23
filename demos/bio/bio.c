// bio.c — 简化版 bio 实现（自研 OS 内核用）
#include "bio.h"
#include "memory.h"
#include "sched.h"
#include "printf.h"
#include "assert.h"

// ============================================================
// bio_vec 操作
// ============================================================

// 初始化 bio_vec
static void bio_vec_init(struct bio_vec *bv, struct page *page,
                         unsigned int len, unsigned int offset) {
    bv->bv_page = page;
    bv->bv_len = len;
    bv->bv_offset = offset;
}

// ============================================================
// bio 分配和释放
// ============================================================

// bio 内存池（slab 缓存，简化：直接 kmalloc）
#define BIO_POOL_SIZE  32
static struct bio bio_pool[BIO_POOL_SIZE];
static unsigned long bio_pool_used[BIO_POOL_SIZE / 64];
static spinlock_t bio_pool_lock;

void bio_pools_init(void) {
    memset(bio_pool, 0, sizeof(bio_pool));
    memset(bio_pool_used, 0, sizeof(bio_pool_used));
    spinlock_init(&bio_pool_lock);
    printf("[bio] bio pool initialized (%d bios)\n", BIO_POOL_SIZE);
}

struct bio *bio_alloc(int nr_vecs, gfp_t gfp_mask) {
    struct bio *bio;
    
    (void)gfp_mask;  // 简化：忽略 gfp_mask
    
    spinlock_lock(&bio_pool_lock);
    
    // 从池子里找一个空闲 bio
    for (int i = 0; i < BIO_POOL_SIZE; i++) {
        int word = i / 64;
        int bit = i % 64;
        if (!(bio_pool_used[word] & (1UL << bit))) {
            bio_pool_used[word] |= (1UL << bit);
            bio = &bio_pool[i];
            goto found;
        }
    }
    
    spinlock_unlock(&bio_pool_lock);
    printf("[bio] ERROR: bio pool exhausted!\n");
    return NULL;

found:
    spinlock_unlock(&bio_pool_lock);
    
    // 初始化 bio
    memset(bio, 0, sizeof(struct bio));
    bio->bi_max_vecs = nr_vecs;
    bio->bi_idx = 0;
    bio->bi_vcnt = 0;
    bio->bi_size = 0;
    bio->bi_op = BIO_OP_READ;
    bio->bi_flags = 0;
    bio->bi_next = NULL;
    bio->bi_private = NULL;
    bio->bi_end_io = NULL;
    bio->bi_bdev = NULL;
    
    // 分配 bio_vec 数组
    if (nr_vecs > 0) {
        bio->bi_io_vec = kmalloc(nr_vecs * sizeof(struct bio_vec), gfp_mask);
        if (!bio->bi_io_vec) {
            printf("[bio] ERROR: failed to alloc bio_vec array\n");
            // 回退
            spinlock_lock(&bio_pool_lock);
            int idx = bio - bio_pool;
            bio_pool_used[idx / 64] &= ~(1UL << (idx % 64));
            spinlock_unlock(&bio_pool_lock);
            return NULL;
        }
    }
    
    return bio;
}

void bio_free(struct bio *bio) {
    if (!bio) return;
    
    // 释放 bio_vec 数组
    if (bio->bi_io_vec) {
        kfree(bio->bi_io_vec);
        bio->bi_io_vec = NULL;
    }
    
    // 放回池子
    spinlock_lock(&bio_pool_lock);
    int idx = bio - bio_pool;
    bio_pool_used[idx / 64] &= ~(1UL << (idx % 64));
    spinlock_unlock(&bio_pool_lock);
}

// ============================================================
// bio_vec 管理
// ============================================================

// 添加一个 page 到 bio（scatter/gather list）
int bio_add_page(struct bio *bio, struct page *page,
                 unsigned int len, unsigned int offset) {
    // 检查是否还能添加
    if (bio->bi_vcnt >= bio->bi_max_vecs) {
        printf("[bio] WARNING: bio_vec array full\n");
        return -ENOMEM;
    }
    
    // 检查物理连续性（简化：不检查）
    // 实际内核要检查相邻 bio_vec 是否物理连续，可以合并
    
    struct bio_vec *bv = &bio->bi_io_vec[bio->bi_vcnt];
    bio_vec_init(bv, page, len, offset);
    
    bio->bi_vcnt++;
    bio->bi_size += len;
    
    return 0;
}

// 简化版：直接添加（不检查合并）
int bio_add_pc_page(struct bio *bio, struct page *page,
                    unsigned int len, unsigned int offset) {
    return bio_add_page(bio, page, len, offset);
}

// ============================================================
// bio 提交
// ============================================================

// 提交 bio 到通用块层
void submit_bio(struct bio *bio) {
    struct block_device *bdev = bio->bi_bdev;
    struct request_queue *q;
    
    if (!bdev) {
        printf("[bio] ERROR: bio with no block device!\n");
        bio_endio(bio, -ENODEV);
        return;
    }
    
    q = bdev->bd_queue;
    if (!q || !q->make_request_fn) {
        printf("[bio] ERROR: no make_request_fn for device!\n");
        bio_endio(bio, -ENODEV);
        return;
    }
    
    // 设置 bio 的目标扇区（从块设备起始地址算起）
    sector_t sector = bio->bi_sector;
    
    printf("[bio] submit: op=%s sector=%llu size=%u vcnt=%d\n",
           bio->bi_op == BIO_OP_READ ? "READ" : "WRITE",
           (unsigned long long)sector,
           bio->bi_size,
           bio->bi_vcnt);
    
    // 调用设备的 make_request_fn（每个设备驱动实现自己的）
    q->make_request_fn(q, bio);
}

// 提交同步 bio（等待 IO 完成）
int submit_bio_wait(struct bio *bio) {
    struct completion done;
    init_completion(&done);
    
    bio->bi_private = &done;
    bio->bi_end_io = bio_complete_callback;
    
    submit_bio(bio);
    
    // 等待完成（简化：spin）
    while (!completion_done(&done)) {
        schedule();
    }
    
    return bio->bi_error;
}

// ============================================================
// bio 回调和完成
// ============================================================

void bio_advance(struct bio *bio, unsigned int bytes) {
    // 推进到下一个 bio_vec
    struct bio_vec *bv = &bio->bi_io_vec[bio->bi_idx];
    
    bv->bv_offset += bytes;
    bv->bv_len -= bytes;
    bio->bi_size -= bytes;
    
    // 当前 bv 耗尽，推进到下一个
    while (bv->bv_len == 0 && bio->bi_idx < bio->bi_vcnt) {
        bio->bi_idx++;
        bv = &bio->bi_io_vec[bio->bi_idx];
    }
}

void bio_endio(struct bio *bio, int error) {
    if (error < 0) {
        bio->bi_flags |= (1UL << BIO_UPTODATE);
    }
    
    if (bio->bi_end_io) {
        bio->bi_end_io(bio, error);
    }
}

// submit_bio_wait 的回调
void bio_complete_callback(struct bio *bio, int error) {
    struct completion *done = bio->bi_private;
    (void)error;
    complete(done);
}

// ============================================================
// bio 克隆和分裂
// ============================================================

struct bio *bio_clone(struct bio *bio, gfp_t gfp_mask) {
    struct bio *clone;
    
    clone = bio_alloc(bio->bi_max_vecs, gfp_mask);
    if (!clone) return NULL;
    
    // 复制元数据
    clone->bi_sector = bio->bi_sector;
    clone->bi_op = bio->bi_op;
    clone->bi_bdev = bio->bi_bdev;
    clone->bi_private = bio->bi_private;
    clone->bi_end_io = bio->bi_end_io;
    clone->bi_idx = 0;
    clone->bi_vcnt = bio->bi_vcnt;
    clone->bi_size = bio->bi_size;
    
    // 克隆 bio_vec 数组（共享 page）
    for (int i = 0; i < bio->bi_vcnt; i++) {
        clone->bi_io_vec[i] = bio->bi_io_vec[i];
    }
    
    return clone;
}

// bio 分裂：当 bio 超过设备最大 IO 大小时分裂成多个
struct bio *bio_split(struct bio *bio, int sectors, gfp_t gfp) {
    struct bio *new_bio;
    int bytes = sectors << 9;  // sectors → bytes
    
    if (bytes >= bio->bi_size) {
        return bio;  // 不需要分裂
    }
    
    new_bio = bio_clone(bio, gfp);
    if (!new_bio) return bio;
    
    // 原 bio 截断到 sectors 字节
    // 新 bio 从 sectors 字节处开始
    new_bio->bi_sector += sectors;
    new_bio->bi_size -= bytes;
    
    // 推进原 bio 的索引（简化版本）
    bio_advance(bio, bytes);
    
    return new_bio;
}

// ============================================================
// request_queue 操作
// ============================================================

static struct request_queue global_queues[BIO_POOL_SIZE];
static spinlock_t queues_lock;

struct request_queue *blk_alloc_queue(gfp_t gfp_mask) {
    struct request_queue *q;
    (void)gfp_mask;
    
    spinlock_lock(&queues_lock);
    for (int i = 0; i < BIO_POOL_SIZE; i++) {
        if (!global_queues[i].bdev) {
            q = &global_queues[i];
            goto found;
        }
    }
    spinlock_unlock(&queues_lock);
    return NULL;
    
found:
    spinlock_unlock(&queues_lock);
    memset(q, 0, sizeof(struct request_queue));
    return q;
}

void blk_queue_make_request(struct request_queue *q, make_request_fn *fn) {
    q->make_request_fn = fn;
}

unsigned int bdev_logical_block_size(struct block_device *bdev) {
    return bdev ? bdev->bd_block_size : 512;
}

unsigned int bdev_io_opt(struct block_device *bdev) {
    // 简化：默认 4KB
    return bdev ? bdev->bd_io_opt : 4096;
}

// ============================================================
// bio 调试和统计
// ============================================================

static atomic_t bio_submit_count = ATOMIC_INIT(0);
static atomic_t bio_complete_count = ATOMIC_INIT(0);

void bio_submit(struct bio *bio) {
    atomic_inc(&bio_submit_count);
    printf("[bio] total submitted: %d\n", atomic_read(&bio_submit_count));
}

void bio_complete(struct bio *bio) {
    atomic_inc(&bio_complete_count);
    printf("[bio] total completed: %d\n", atomic_read(&bio_complete_count));
}

int bio_get_stats(void) {
    printf("[bio] stats: submitted=%d completed=%d\n",
           atomic_read(&bio_submit_count),
           atomic_read(&bio_complete_count));
    return 0;
}
