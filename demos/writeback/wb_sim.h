// wb_sim.h — 简化的 writeback 实现（自研 OS 内核用）
#ifndef WB_SIM_H
#define WB_SIM_H

#include "types.h"
#include "list.h"
#include "spinlock.h"

// 前向声明
struct page;
struct inode;
struct block_device;

// 脏页描述符
struct dirty_page {
    struct page *page;              // 指向 page cache 页
    struct inode *inode;           // 属于哪个 inode
    u64 offset;                     // 文件内的偏移量（字节）
    struct list_node dirty_node;   // 链入 inode 的 dirty list
    bool synced;                   // 是否已回写到磁盘
};

// inode 的回写状态
struct inode_wb_state {
    struct list_node dirty_pages;  // inode 的所有 dirty page
    spinlock_t lock;               // 保护 dirty_pages
    u32 nr_dirty;                 // dirty page 数量
    bool writeback_in_progress;   // 是否正在回写
    bool flush_pending;           // 是否需要强制回写（fsync）
};

// bdi_writeback — 一个块设备的回写队列
struct bdi_writeback {
    struct list_node b_dirty;      // 所有 dirty inode
    struct list_node b_io;        // 正在回写的 inode
    struct list_node b_more_io;   // 等待回写的 inode
    struct block_device *bdi;     // 关联的块设备
    u64 flush_ts;                  // 上次 flush 时间戳
};

// ============================================================
// 接口函数
// ============================================================

// 初始化 inode 的回写状态
void inode_wb_state_init(struct inode_wb_state *state);

// 标记页面为脏（加入 inode 的 dirty list）
void mark_page_dirty(struct page *page, struct inode *inode);

// 取消页面脏标记
void unmark_page_dirty(struct page *page, struct inode *inode);

// 对单个 inode 执行回写（fsync 路径）
void writeback_inode(struct inode *inode);

// 对所有 inode 执行全局 flush（sync 路径）
void writeback_all_inodes(void);

// 刷新指定 inode 并等待完成（对应用户空间 fsync）
int fsync_inode(struct inode *inode);

// 全局 flush 线程入口
void flush_thread_entry(void *arg);

// 初始化 writeback 子系统
void writeback_init(void);

// 检查是否超过 dirty ratio 阈值
bool should_start_writeback(void);

// 获取全局 dirty page 总数
u64 global_dirty_page_count(void);

#endif // WB_SIM_H
