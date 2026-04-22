// wb_sim.c — 简化的 writeback 实现（自研 OS 内核用）
#include "wb_sim.h"
#include "page_cache.h"
#include "block_device.h"
#include "sched.h"
#include "timer.h"
#include "memory.h"

// ============================================================
// 全局变量
// ============================================================

// 全局回写队列（对应一个块设备）
static struct bdi_writeback global_wb;

// dirty_ratio 阈值（Dirty 占可用内存的百分比）
#define DIRTY_RATIO_THRESHOLD     20   // 超过则阻塞写进程
#define DIRTY_BACKGROUND_RATIO    10   // 超过则后台 flush 启动

// flush 检查间隔（毫秒）
#define FLUSH_CHECK_INTERVAL_MS   5000

// 全局 dirty page 计数器（简化：实际要 per-cpu 统计）
static u64 g_dirty_count = 0;
static spinlock_t g_dirty_count_lock;

// ============================================================
// 初始化
// ============================================================

void inode_wb_state_init(struct inode_wb_state *state) {
    init_list_head(&state->dirty_pages);
    spinlock_init(&state->lock);
    state->nr_dirty = 0;
    state->writeback_in_progress = false;
    state->flush_pending = false;
}

void writeback_init(void) {
    init_list_head(&global_wb.b_dirty);
    init_list_head(&global_wb.b_io);
    init_list_head(&global_wb.b_more_io);
    global_wb.bdi = NULL;
    global_wb.flush_ts = 0;
    spinlock_init(&g_dirty_count_lock);
    
    // 创建 flush 内核线程
    kernel_thread(flush_thread_entry, NULL, KERNEL_THREAD_FLAGS);
    printf("[writeback] initialized, flush thread started\n");
}

// ============================================================
// 脏页管理
// ============================================================

void mark_page_dirty(struct page *page, struct inode *inode) {
    struct inode_wb_state *state = inode->wb_state;
    
    spinlock_lock(&state->lock);
    
    // 避免重复标记
    if (page->dirty) {
        spinlock_unlock(&state->lock);
        return;
    }
    
    // 获取 dirty_page 描述符（page->private 指向它）
    struct dirty_page *dp = page->private;
    dp->inode = inode;
    dp->offset = page->index * PAGE_SIZE;
    dp->synced = false;
    
    // 加入 inode 的 dirty list
    list_add_tail(&dp->dirty_node, &state->dirty_pages);
    page->dirty = true;
    state->nr_dirty++;
    
    // 更新全局计数
    spinlock_lock(&g_dirty_count_lock);
    g_dirty_count++;
    spinlock_unlock(&g_dirty_count_lock);
    
    // 如果 inode 不在全局 dirty list 里，加入
    // （简化：实际要跟踪 inode 是否已在 b_dirty）
    
    spinlock_unlock(&state->lock);
}

void unmark_page_dirty(struct page *page, struct inode *inode) {
    struct inode_wb_state *state = inode->wb_state;
    
    spinlock_lock(&state->lock);
    
    if (!page->dirty) {
        spinlock_unlock(&state->lock);
        return;
    }
    
    struct dirty_page *dp = page->private;
    
    // 从 inode 的 dirty list 移除
    list_del(&dp->dirty_node);
    page->dirty = false;
    state->nr_dirty--;
    
    // 更新全局计数
    spinlock_lock(&g_dirty_count_lock);
    g_dirty_count--;
    spinlock_unlock(&g_dirty_count_lock);
    
    spinlock_unlock(&state->lock);
}

// ============================================================
// 回写逻辑
// ============================================================

// 判断是否超过 dirty_ratio 阈值
bool should_start_writeback(void) {
    u64 available = get_available_memory_kb();
    u64 dirty;
    
    spinlock_lock(&g_dirty_count_lock);
    dirty = g_dirty_count * (PAGE_SIZE / 1024);  // dirty page 数 → KB
    spinlock_unlock(&g_dirty_count_lock);
    
    if (available == 0) return true;
    
    return (dirty * 100 / available) >= DIRTY_BACKGROUND_RATIO;
}

u64 global_dirty_page_count(void) {
    spinlock_lock(&g_dirty_count_lock);
    u64 count = g_dirty_count;
    spinlock_unlock(&g_dirty_count_lock);
    return count;
}

// 回写单个 inode 的所有 dirty pages
void writeback_inode(struct inode *inode) {
    struct inode_wb_state *state = inode->wb_state;
    struct list_node *pos, *n;
    
    spinlock_lock(&state->lock);
    state->writeback_in_progress = true;
    
    // 遍历 inode 的所有 dirty page
    list_for_each_safe(pos, n, &state->dirty_pages) {
        struct dirty_page *dp = list_entry(pos, struct dirty_page, dirty_node);
        
        // 从 dirty list 移除（先解锁再刷盘）
        list_del(&dp->dirty_node);
        dp->synced = false;
        state->nr_dirty--;
        
        spinlock_unlock(&state->lock);
        
        // 实际写入块设备
        block_device_write_page(inode->bdev, dp->page, dp->offset);
        dp->synced = true;
        
        // 清除 page 的 dirty 标志
        dp->page->dirty = false;
        
        // 更新全局计数
        spinlock_lock(&g_dirty_count_lock);
        g_dirty_count--;
        spinlock_unlock(&g_dirty_count_lock);
        
        spinlock_lock(&state->lock);
    }
    
    state->writeback_in_progress = false;
    spinlock_unlock(&state->lock);
}

// 对所有 dirty inode 执行全局 flush（对应 sync()）
void writeback_all_inodes(void) {
    struct list_node *pos, *n;
    
    // 遍历所有 inode（简化：实际要从 super_block 遍历）
    // 这里假设有 all_inodes 链表
    list_for_each_safe(pos, n, &all_inodes) {
        struct inode *inode = list_entry(pos, struct inode, inode_list);
        struct inode_wb_state *state = inode->wb_state;
        
        spinlock_lock(&state->lock);
        if (state->nr_dirty > 0) {
            spinlock_unlock(&state->lock);
            writeback_inode(inode);
        } else {
            spinlock_unlock(&state->lock);
        }
    }
}

// 对指定 inode 执行 fsync（等待回写完成）
int fsync_inode(struct inode *inode) {
    struct inode_wb_state *state = inode->wb_state;
    
    spinlock_lock(&state->lock);
    state->flush_pending = true;
    spinlock_unlock(&state->lock);
    
    // 先触发一次 inode 回写
    if (state->nr_dirty > 0) {
        writeback_inode(inode);
    }
    
    // 等待回写完成（简化：实际要等待 bio 完成回调）
    while (1) {
        spinlock_lock(&state->lock);
        if (state->nr_dirty == 0 && !state->writeback_in_progress) {
            spinlock_unlock(&state->lock);
            break;
        }
        spinlock_unlock(&state->lock);
        
        // 让出 CPU，等待 I/O 完成
        schedule();
    }
    
    return 0;
}

// ============================================================
// flush 线程
// ============================================================

void flush_thread_entry(void *arg) {
    (void)arg;
    
    printf("[flush] thread started (interval=%dms)\n", FLUSH_CHECK_INTERVAL_MS);
    
    while (1) {
        // 睡眠 FLUSH_CHECK_INTERVAL_MS
        msleep(FLUSH_CHECK_INTERVAL_MS);
        
        // 检查是否超过 dirty_ratio
        if (should_start_writeback()) {
            printf("[flush] dirty threshold exceeded, starting writeback\n");
            
            struct list_node *pos, *n;
            list_for_each_safe(pos, n, &all_inodes) {
                struct inode *inode = list_entry(pos, struct inode, inode_list);
                struct inode_wb_state *state = inode->wb_state;
                
                spinlock_lock(&state->lock);
                if (state->nr_dirty > 0) {
                    spinlock_unlock(&state->lock);
                    writeback_inode(inode);
                } else {
                    spinlock_unlock(&state->lock);
                }
            }
            
            printf("[flush] writeback complete, dirty_count=%lu\n", 
                   global_dirty_page_count());
        }
    }
}
