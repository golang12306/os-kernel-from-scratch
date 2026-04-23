// iosched.h — 简化版 IO 调度器接口
#ifndef IOSCHED_H
#define IOSCHED_H

#include "types.h"
#include "list.h"
#include "bio.h"

// ============================================================
// request：bio 的聚合体（一个磁盘 IO 操作）
// ============================================================

#define REQ_PRIO_MAX   16  // 最大优先级

struct request {
    sector_t sector;            // 起始扇区
    unsigned int nr_sectors;    // 扇区数
    unsigned int bytes;         // 字节数
    
    unsigned short op;           // BIO_OP_READ / WRITE
    unsigned long flags;
    
    struct bio *bio;            // 关联的 bio（多个 bio 可能合并成一个 request）
    struct list_head queuelist;  // 调度队列链表
    struct list_head fiolist;    // fifo 链表（按时间排序）
    
    unsigned long jiffies;      // 加入队列的时间
    int refcount;              // 引用计数
};

// ============================================================
// IO 调度器接口
// ============================================================

// 调度器算法
enum {
    IOSCHED_NONE = 0,    // noop：不做调度，按 FIFO
    IOSCHED_DEADLINE,    // deadline：保证延迟
    IOSCHED_CFQ,         // cfq：完全公平队列
};

// IO 调度器结构
struct io_scheduler {
    const char *name;
    int algorithm;
    
    // 调度队列（按扇区排序）
    struct list_head dispatch;  // 等待派发的 request
    struct list_head fifo_list; // 按时间排队的 request
    
    unsigned int queue_depth;   // 队列深度
    unsigned long flags;
    
    // 算法特定数据
    void *private_data;
    
    // 调度器方法
    void (*init_fn)(struct io_scheduler *sched);
    void (*exit_fn)(struct io_scheduler *sched);
    void (*add_request_fn)(struct io_scheduler *sched, struct request *req);
    struct request *(*dispatch_fn)(struct io_scheduler *sched);
    void (*finish_request_fn)(struct io_scheduler *sched, struct request *req);
};

// ============================================================
// 全局调度器实例
// ============================================================

extern struct io_scheduler *current_iosched;

// 初始化默认调度器
void io_sched_init(void);

// 选择调度器
int io_sched_set(const char *name);

// 添加 request 到调度器
void iosched_add_request(struct request *req);

// 从调度器取出一个 request 派发
struct request *iosched_dispatch(void);

// 完成 request
void iosched_finish_request(struct request *req);

// ============================================================
// 辅助函数
// ============================================================

// 判断两个 request 是否可以合并
static inline bool request_can_merge(struct request *a, struct request *b) {
    // 必须是同类型操作
    if (a->op != b->op) return false;
    
    // 必须相邻（a 的结束 == b 的开始，或 b 的结束 == a 的开始）
    sector_t a_end = a->sector + (a->nr_sectors);
    sector_t b_end = b->sector + (b->nr_sectors);
    
    return (a_end == b->sector) || (b_end == a->sector);
}

// 合并两个 request
static inline void request_merge(struct request *a, struct request *b) {
    // 假设 a 在前，b 在后
    if (b->sector < a->sector) {
        // 交换
        a->sector = b->sector;
    }
    a->nr_sectors = max(a->nr_sectors + b->nr_sectors,
                         (unsigned int)(b->sector + b->nr_sectors - a->sector));
    a->bytes += b->bytes;
}

// 获取 request 的结束扇区
static inline sector_t request_end(struct request *req) {
    return req->sector + req->nr_sectors;
}

#endif // IOSCHED_H
