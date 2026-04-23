# IO Scheduler Demo

## 文章对应代码

- `iosched.h` — IO 调度器核心数据结构
- `iosched.c` — noop / deadline / cfq 三种调度器实现

## 源码文件说明

### iosched.h

```c
struct request {
    sector_t sector;        // 起始扇区
    unsigned int nr_sectors; // 扇区数
    unsigned int bytes;     // 字节数
    struct bio *bio;        // 关联的 bio
    struct list_head queuelist; // 调度队列链表
    struct list_head fiolist;   // 时间 FIFO 链表
};

struct io_scheduler {
    const char *name;
    int algorithm;
    struct list_head dispatch;  // 派发队列
    void (*init_fn)(...);
    void (*add_request_fn)(...);
    struct request *(*dispatch_fn)(...);
    void (*finish_request_fn)(...);
};
```

### iosched.c

三种调度器实现：

- **noop** — 纯 FIFO，不排序不合并，适合 SSD
- **deadline** — 三队列结构（读 FIFO + 写 FIFO + 排序队列），保证延迟上限
- **cfq** — 每进程一个队列，轮转派发，完全公平（已被移除）

核心函数：

- `iosched_add_request()` — 添加 request 到调度器
- `iosched_dispatch()` — 取出下一个要执行的 request
- `iosched_finish_request()` — request 完成后的清理

---

## 文章核心内容

- IO 调度器 = bio 的聚合 + 排序 + 派发
- noop：FIFO，无优化，适合 SSD
- deadline：三队列 + 截止时间保证，不让任何人饿死
- cfq：进程级公平队列，轮转时间片（已被 bfq 取代）
- 合并（front/back merge）+ 排序（电梯算法）
