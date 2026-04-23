// iosched.c — 简化版 IO 调度器实现
#include "iosched.h"
#include "memory.h"
#include "printf.h"
#include "assert.h"

// ============================================================
// 全局变量
// ============================================================

struct io_scheduler *current_iosched = NULL;

// noop 调度器
static struct io_scheduler noop_sched = {
    .name = "noop",
    .algorithm = IOSCHED_NONE,
    .init_fn = noop_init,
    .exit_fn = noop_exit,
    .add_request_fn = noop_add_request,
    .dispatch_fn = noop_dispatch,
    .finish_request_fn = noop_finish_request,
};

// deadline 调度器
static struct io_scheduler deadline_sched = {
    .name = "deadline",
    .algorithm = IOSCHED_DEADLINE,
    .init_fn = deadline_init,
    .exit_fn = deadline_exit,
    .add_request_fn = deadline_add_request,
    .dispatch_fn = deadline_dispatch,
    .finish_request_fn = deadline_finish_request,
};

// cfq 调度器
static struct io_scheduler cfq_sched = {
    .name = "cfq",
    .algorithm = IOSCHED_CFQ,
    .init_fn = cfq_init,
    .exit_fn = cfq_exit,
    .add_request_fn = cfq_add_request,
    .dispatch_fn = cfq_dispatch,
    .finish_request_fn = cfq_finish_request,
};

// ============================================================
// 通用 request 管理
// ============================================================

struct request *alloc_request(struct bio *bio) {
    struct request *req;
    
    req = kmalloc(sizeof(struct request), GFP_KERNEL);
    if (!req) return NULL;
    
    req->sector = bio->bi_sector;
    req->nr_sectors = bio->bi_size >> 9;
    req->bytes = bio->bi_size;
    req->op = bio->bi_op;
    req->bio = bio;
    req->jiffies = get_jiffies();
    req->refcount = 1;
    init_list_head(&req->queuelist);
    init_list_head(&req->fiolist);
    
    return req;
}

void free_request(struct request *req) {
    if (!req) return;
    kfree(req);
}

// ============================================================
// NOOP 调度器
// ============================================================

// noop 不做任何排序，只是 FIFO
// 适合 SSD（无需磁头寻道优化）

void noop_init(struct io_scheduler *sched) {
    init_list_head(&sched->dispatch);
    init_list_head(&sched->fifo_list);
    printf("[iosched] noop scheduler initialized\n");
}

void noop_exit(struct io_scheduler *sched) {
    printf("[iosched] noop scheduler exiting\n");
}

void noop_add_request(struct io_scheduler *sched, struct request *req) {
    list_add_tail(&req->queuelist, &sched->dispatch);
    printf("[noop] added req sector=%llu\n", (unsigned long long)req->sector);
}

struct request *noop_dispatch(struct io_scheduler *sched) {
    struct request *req;
    
    if (list_empty(&sched->dispatch)) {
        return NULL;
    }
    
    // FIFO：取队列头部
    req = list_entry(sched->dispatch.next, struct request, queuelist);
    list_del(&req->queuelist);
    
    printf("[noop] dispatch sector=%llu\n", (unsigned long long)req->sector);
    return req;
}

void noop_finish_request(struct io_scheduler *sched, struct request *req) {
    printf("[noop] finished sector=%llu\n", (unsigned long long)req->sector);
    free_request(req);
}

// ============================================================
// DEADLINE 调度器
// ============================================================

// deadline 保证每类 IO 的延迟上限
// 读请求：max_read延迟（比如 500ms）
// 写请求：max_write延迟（比如 5s）
// 先检查是否有快过期的请求，有则优先派发
// 否则按扇区顺序派发（电梯算法）

#define DEADLINE_DEFAULT_READ_DEADLINE  500   // ms
#define DEADLINE_DEFAULT_WRITE_DEADLINE 5000  // ms

struct deadline_data {
    struct list_head read_fifo;   // 读请求 FIFO（按时间）
    struct list_head write_fifo; // 写请求 FIFO（按时间）
    unsigned int read_deadline;
    unsigned int write_deadline;
    unsigned long current_jiffies;
};

static struct deadline_data dd;

void deadline_init(struct io_scheduler *sched) {
    init_list_head(&sched->dispatch);
    init_list_head(&dd.read_fifo);
    init_list_head(&dd.write_fifo);
    dd.read_deadline = DEADLINE_DEFAULT_READ_DEADLINE;
    dd.write_deadline = DEADLINE_DEFAULT_WRITE_DEADLINE;
    
    sched->private_data = &dd;
    printf("[iosched] deadline scheduler initialized\n");
}

void deadline_exit(struct io_scheduler *sched) {
    printf("[iosched] deadline scheduler exiting\n");
}

void deadline_add_request(struct io_scheduler *sched, struct request *req) {
    struct deadline_data *dd = sched->private_data;
    
    // 加入对应优先级的 FIFO
    if (req->op == BIO_OP_READ) {
        list_add_tail(&req->fiolist, &dd->read_fifo);
        printf("[deadline] added READ req sector=%llu to read_fifo\n",
               (unsigned long long)req->sector);
    } else {
        list_add_tail(&req->fiolist, &dd->write_fifo);
        printf("[deadline] added WRITE req sector=%llu to write_fifo\n",
               (unsigned long long)req->sector);
    }
    
    // 同时加入扇区排序队列
    struct request *pos;
    bool inserted = false;
    list_for_each_entry(pos, &sched->dispatch, queuelist) {
        if (req->sector < pos->sector) {
            list_add_tail(&req->queuelist, &pos->queuelist);
            inserted = true;
            break;
        }
    }
    if (!inserted) {
        list_add_tail(&req->queuelist, &sched->dispatch);
    }
}

static bool deadline_read_expired(struct deadline_data *dd) {
    if (list_empty(&dd->read_fifo)) return false;
    
    struct request *req = list_entry(dd->read_fifo.next, 
                                     struct request, fiolist);
    unsigned long age = jiffies_to_msecs(get_jiffies() - req->jiffies);
    return age >= dd->read_deadline;
}

static bool deadline_write_expired(struct deadline_data *dd) {
    if (list_empty(&dd->write_fifo)) return false;
    
    struct request *req = list_entry(dd->write_fifo.next,
                                     struct request, fiolist);
    unsigned long age = jiffies_to_msecs(get_jiffies() - req->jiffies);
    return age >= dd->write_deadline;
}

struct request *deadline_dispatch(struct io_scheduler *sched) {
    struct deadline_data *dd = sched->private_data;
    struct request *req;
    
    // 1. 检查读 FIFO 是否过期（读优先，因为应用程序常等待读完成）
    if (deadline_read_expired(dd)) {
        req = list_entry(dd->read_fifo.next, struct request, fiolist);
        list_del(&req->fiolist);
        list_del(&req->queuelist);
        printf("[deadline] dispatch EXPIRED READ req sector=%llu\n",
               (unsigned long long)req->sector);
        return req;
    }
    
    // 2. 检查写 FIFO 是否过期
    if (deadline_write_expired(dd)) {
        req = list_entry(dd->write_fifo.next, struct request, fiolist);
        list_del(&req->fiolist);
        list_del(&req->queuelist);
        printf("[deadline] dispatch EXPIRED WRITE req sector=%llu\n",
               (unsigned long long)req->sector);
        return req;
    }
    
    // 3. 正常情况：按扇区顺序派发（电梯算法）
    if (!list_empty(&sched->dispatch)) {
        req = list_entry(sched->dispatch.next, struct request, queuelist);
        list_del(&req->queuelist);
        
        // 从对应 FIFO 移除
        list_del(&req->fiolist);
        
        printf("[deadline] dispatch sorted READ req sector=%llu\n",
               (unsigned long long)req->sector);
        return req;
    }
    
    return NULL;
}

void deadline_finish_request(struct io_scheduler *sched, struct request *req) {
    printf("[deadline] finished sector=%llu\n", (unsigned long long)req->sector);
    free_request(req);
}

// ============================================================
// CFQ 调度器（完全公平队列）
// ============================================================

// CFQ 把 IO 带宽均匀分配给每个进程/进程组
// 每个进程有自己的 IO 队列
// 调度器轮转各进程队列，每次派发一个时间片（比如 100ms）的请求

#define CFQ_DEFAULT_TIME_SLICE 100    // ms
#define CFQ_DEFAULT_QUEUE_DEPTH  4    // 每个进程最大队列深度

struct cfq_queue {
    pid_t pid;                   // 进程 ID
    char comm[16];              // 进程名
    struct list_head queue;     // 该进程的请求队列
    unsigned int dispatched;    // 已派发的扇区数
    unsigned long last_jiffies; // 上次派发时间
    unsigned int weight;        // 权重（默认 100）
    bool was_active;           // 上个调度周期是否活跃
    struct list_head cfq_list;  // 加入调度器队列
};

struct cfq_data {
    struct list_head cfq_queues;   // 所有 cfq 队列
    struct cfq_queue *active_queue; // 当前活跃队列
    struct list_head *service_tree; // 服务树（按权重组织）
    unsigned long current_jiffies;
    unsigned int cfq_slice;
};

static struct cfq_data cfqd;

struct cfq_queue *cfq_get_queue(struct cfq_data *cfqd, pid_t pid) {
    // 查找或创建进程的 cfq 队列
    struct cfq_queue *cfqq;
    
    list_for_each_entry(cfqq, &cfqd->cfq_queues, cfq_list) {
        if (cfqq->pid == pid) {
            return cfqq;
        }
    }
    
    // 创建新队列
    cfqq = kmalloc(sizeof(struct cfq_queue), GFP_KERNEL);
    if (!cfqq) return NULL;
    
    memset(cfqq, 0, sizeof(struct cfq_queue));
    cfqq->pid = pid;
    init_list_head(&cfqq->queue);
    cfqq->weight = 100;
    list_add_tail(&cfqq->cfq_list, &cfqd->cfq_queues);
    
    printf("[cfq] created queue for pid=%d\n", pid);
    return cfqq;
}

void cfq_init(struct io_scheduler *sched) {
    init_list_head(&sched->dispatch);
    init_list_head(&cfqd.cfq_queues);
    cfqd.active_queue = NULL;
    cfqd.cfq_slice = CFQ_DEFAULT_TIME_SLICE;
    
    sched->private_data = &cfqd;
    printf("[iosched] cfq scheduler initialized\n");
}

void cfq_exit(struct io_scheduler *sched) {
    printf("[iosched] cfq scheduler exiting\n");
}

void cfq_add_request(struct io_scheduler *sched, struct request *req) {
    struct cfq_data *cfqd = sched->private_data;
    struct cfq_queue *cfqq;
    
    // 获取进程的 cfq 队列
    pid_t pid = current->pid;
    cfqq = cfq_get_queue(cfqd, pid);
    if (!cfqq) return;
    
    // 加入进程的请求队列（按扇区排序）
    struct request *pos;
    bool inserted = false;
    list_for_each_entry(pos, &cfqq->queue, queuelist) {
        if (req->sector < pos->sector) {
            list_add_tail(&req->queuelist, &pos->queuelist);
            inserted = true;
            break;
        }
    }
    if (!inserted) {
        list_add_tail(&req->queuelist, &cfqq->queue);
    }
    
    printf("[cfq] pid=%d added req sector=%llu\n",
           pid, (unsigned long long)req->sector);
}

struct request *cfq_dispatch(struct io_scheduler *sched) {
    struct cfq_data *cfqd = sched->private_data;
    struct cfq_queue *cfqq;
    struct request *req;
    
    // 轮转调度：找下一个活跃队列
    if (!cfqd->active_queue || cfqd->active_queue->dispatched >= cfqd->cfq_slice) {
        // 时间片用完，切换到下一个队列
        list_for_each_entry(cfqq, &cfqd->cfq_queues, cfq_list) {
            if (!list_empty(&cfqq->queue)) {
                cfqd->active_queue = cfqq;
                cfqq->dispatched = 0;
                break;
            }
        }
    }
    
    cfqq = cfqd->active_queue;
    if (!cfqq || list_empty(&cfqq->queue)) {
        return NULL;
    }
    
    // 从活跃队列取一个 request
    req = list_entry(cfqq->queue.next, struct request, queuelist);
    list_del(&req->queuelist);
    
    cfqq->dispatched += req->bytes;
    cfqq->last_jiffies = get_jiffies();
    
    printf("[cfq] dispatch from pid=%d sector=%llu (dispatched=%u/%u)\n",
           cfqq->pid, (unsigned long long)req->sector,
           cfqq->dispatched, cfqd->cfq_slice);
    
    return req;
}

void cfq_finish_request(struct io_scheduler *sched, struct request *req) {
    printf("[cfq] finished sector=%llu\n", (unsigned long long)req->sector);
    free_request(req);
}

// ============================================================
// 全局调度器接口
// ============================================================

void io_sched_init(void) {
    // 默认用 deadline
    current_iosched = &deadline_sched;
    current_iosched->init_fn(current_iosched);
    printf("[iosched] default scheduler: deadline\n");
}

int io_sched_set(const char *name) {
    if (strcmp(name, "noop") == 0) {
        current_iosched = &noop_sched;
    } else if (strcmp(name, "deadline") == 0) {
        current_iosched = &deadline_sched;
    } else if (strcmp(name, "cfq") == 0) {
        current_iosched = &cfq_sched;
    } else {
        printf("[iosched] unknown scheduler '%s'\n", name);
        return -EINVAL;
    }
    
    if (current_iosched->init_fn) {
        current_iosched->init_fn(current_iosched);
    }
    
    printf("[iosched] switched to '%s'\n", name);
    return 0;
}

void iosched_add_request(struct request *req) {
    if (current_iosched && current_iosched->add_request_fn) {
        current_iosched->add_request_fn(current_iosched, req);
    }
}

struct request *iosched_dispatch(void) {
    if (current_iosched && current_iosched->dispatch_fn) {
        return current_iosched->dispatch_fn(current_iosched);
    }
    return NULL;
}

void iosched_finish_request(struct request *req) {
    if (current_iosched && current_iosched->finish_request_fn) {
        current_iosched->finish_request_fn(current_iosched, req);
    }
}
