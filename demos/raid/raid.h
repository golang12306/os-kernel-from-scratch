// raid.h — 简化版 RAID 接口（自研 OS 内核用）
#ifndef RAID_H
#define RAID_H

#include "types.h"
#include "list.h"
#include "bio.h"
#include "block_device.h"

// ============================================================
// RAID 级别
// ============================================================

#define RAID_LEVEL_NONE   0
#define RAID_LEVEL_0     1   // 条带化，无冗余
#define RAID_LEVEL_1     2   // 镜像
#define RAID_LEVEL_4     3   // 专用奇偶校验
#define RAID_LEVEL_5     4   // 分布式奇偶校验
#define RAID_LEVEL_6     5   // 双奇偶校验
#define RAID_LEVEL_10    6   // 条带化 + 镜像

// ============================================================
// RAID 设备结构
// ============================================================

struct raid_device {
    const char *name;          // RAID 设备名，如 "md0"
    int level;                // RAID 级别
    int raid_disks;           // 数据盘数量
    int total_disks;          // 总盘数（含热备盘）
    
    sector_t size;            // RAID 逻辑大小（扇区）
    sector_t chunk_size;      // 条带大小（字节）
    
    // 成员盘
    struct block_device **disks;
    int num_disks;
    
    // 热备盘
    struct block_device **spare_disks;
    int num_spares;
    
    // RAID 状态
    unsigned long state;       // 状态标志
    struct list_head component_devices;  // 组件设备链表
    
    // 操作函数
    struct raid_ops *ops;
};

// RAID 操作接口
struct raid_ops {
    // 读数据（计算应从哪个盘读）
    sector_t (*map_sector)(struct raid_device *raid, sector_t sector);
    
    // 写数据（计算应写哪些盘）
    int (*map_write)(struct raid_device *raid, sector_t sector,
                     int *target_disks, int *num_disks);
    
    // 重建
    int (*rebuild)(struct raid_device *raid, int failed_disk);
    
    // 计算奇偶校验
    void (*calc_parity)(struct raid_device *raid, struct bio **bios,
                         int count, struct bio *p_bio);
};

// ============================================================
// RAID 状态标志
// ============================================================

#define RAID_STATE_CLEAN       0
#define RAID_STATE_DIRTY       1
#define RAID_STATE_RESYNCING  2
#define RAID_STATE_DEGRADED   3
#define RAID_STATE_FAILED      4

// ============================================================
// 接口函数
// ============================================================

// 创建 RAID
struct raid_device *raid_create(const char *name, int level,
                                 struct block_device **disks, int num_disks,
                                 sector_t chunk_size);

// 销毁 RAID
void raid_destroy(struct raid_device *raid);

// 启动 RAID（从成员盘重建元数据）
int raid_run(struct raid_device *raid);

// 停止 RAID
int raid_stop(struct raid_device *raid);

// 读取 RAID 逻辑扇区（调度到成员盘）
int raid_read(struct raid_device *raid, struct bio *bio);

// 写入 RAID 逻辑扇区（计算条带/奇偶校验）
int raid_write(struct raid_device *raid, struct bio *bio);

// 标记 RAID 盘为故障
int raid_set_faulty(struct raid_device *raid, int disk_index);

// 添加热备盘
int raid_add_spare(struct raid_device *raid, struct block_device *spare);

// 触发 RAID 重建
int raid_rebuild(struct raid_device *raid);

// 获取 RAID 状态信息
int raid_status(struct raid_device *raid);

// 计算 RAID 逻辑块大小的条带映射
int raid_chunk_map(struct raid_device *raid, sector_t sector,
                   int *disk_idx, sector_t *disk_sector,
                   int *disk_count, sector_t *disk_offset);

#endif // RAID_H
