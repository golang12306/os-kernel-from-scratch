// bio.h — 简化版 bio 结构（自研 OS 内核用）
#ifndef BIO_H
#define BIO_H

#include "types.h"
#include "list.h"
#include "page.h"

// ============================================================
// 块设备 IO 向量（bio_vec）
// ============================================================

// bio_vec：描述一段连续的内存区域（对应一个物理页的一部分或全部）
struct bio_vec {
    struct page *bv_page;     // 内存页
    unsigned int bv_len;       // 长度（字节）
    unsigned int bv_offset;    // 页内偏移（字节）
};

// ============================================================
// bio 结构（块 IO 请求）
// ============================================================

// bio 操作类型
#define BIO_OP_READ      0  // 读
#define BIO_OP_WRITE     1  // 写
#define BIO_OP_FLUSH     2  // 刷新（写回缓存）
#define BIO_OP_DISCARD   3  // 丢弃

// bio 状态标志
#define BIO_UPTODATE     0  // IO 完成，数据有效
#define BIO_QUIET        1  // 不输出错误信息
#define BIO_SEG_VALID    2  // bio_vec 数组有效
#define BIO_CLONED       3  // bio 是克隆的

// 完成的回调函数类型
typedef void (bio_end_io_t)(struct bio *, int);

// bio：块设备 IO 的基本单位
struct bio {
    sector_t bi_sector;          // 起始扇区号（磁盘地址）
    struct bio_vec *bi_io_vec;   // 内存页数组（scatter/gather list）
    unsigned short bi_vcnt;      // bio_vec 数组长度
    unsigned short bi_max_vecs;  // 最大 bio_vec 数（通常 = 256）
    
    unsigned int bi_size;         // 总字节数（所有 bio_vec 的总和）
    unsigned int bi_phys_segments; // 物理上不连续的段数
    unsigned int bi_idx;          // 当前处理到的 bio_vec 索引
    
    // 操作类型 + 状态标志
    unsigned short bi_op;         // BIO_OP_READ / WRITE / FLUSH / DISCARD
    unsigned long bi_flags;       // 状态标志
    
    struct request *bi_next;     // 链到下一个 bio（用于合并）
    
    // 私有数据（由设备驱动使用）
    void *bi_private;
    
    // 完成回调
    bio_end_io_t *bi_end_io;
    
    // 关联的块设备
    struct block_device *bi_bdev;
    
    // 用于回收的链表节点
    struct list_head bi_list;
};

// ============================================================
// bio 操作接口
// ============================================================

// 分配一个 bio
struct bio *bio_alloc(int nr_vecs, gfp_t gfp_mask);

// 释放一个 bio
void bio_free(struct bio *bio);

// 添加一个 page 到 bio（组成 scatter/gather list）
int bio_add_page(struct bio *bio, struct page *page, 
                 unsigned int len, unsigned int offset);

// 向 bio 添加一个物理页（底层版本）
int bio_add_pc_page(struct bio *bio, struct page *page,
                    unsigned int len, unsigned int offset);

// 克隆一个 bio
struct bio *bio_clone(struct bio *bio, gfp_t gfp_mask);

// 分割 bio（当 bio 超过设备最大 IO 大小时）
struct bio *bio_split(struct bio *bio, int sectors, gfp_t gfp);

// 提交 bio 到通用块层
void submit_bio(struct bio *bio);

// 提交同步 bio（等待完成才返回）
int submit_bio_wait(struct bio *bio);

// bio 完成回调：推进到下一个 bio_vec
void bio_advance(struct bio *bio, unsigned int bytes);

// bio 完成回调：标记完成
void bio_endio(struct bio *bio, int error);

// 获取 bio 的剩余字节数
#define bio_sectors(bio)  ((bio)->bi_size >> 9)
#define bio_bytes(bio)    ((bio)->bi_size)

// 判断 bio 是否还有未处理的数据
#define bio_has_data(bio) ((bio)->bi_size > 0)

// ============================================================
// 请求（request）— 由 bio 组成
// ============================================================

struct request;
struct request_queue;

// 请求队列（每个块设备一个）
struct request_queue {
    struct block_device *bdev;    // 关联的块设备
    unsigned long queue_flags;     // 队列标志
    unsigned int queue_depth;      // 队列深度
    make_request_fn *make_request_fn; // 处理 bio 的函数指针
    void *queuedata;
};

// 分配请求队列
struct request_queue *blk_alloc_queue(gfp_t gfp_mask);

// 设置请求队列的 make_request_fn
void blk_queue_make_request(struct request_queue *q, make_request_fn *fn);

// 获取设备的逻辑块大小（字节）
unsigned int bdev_logical_block_size(struct block_device *bdev);

// 获取设备的最大 IO 大小（字节）
unsigned int bdev_io_opt(struct block_device *bdev);

#endif // BIO_H
