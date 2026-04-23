// lvm.h — 简化版 LVM（Logical Volume Manager）结构与接口
#ifndef LVM_H
#define LVM_H

#include "types.h"
#include "block_device.h"

// ============================================================
// LVM 核心概念
// ============================================================
//
// 物理卷（Physical Volume, PV）：
//   物理磁盘或分区，如 /dev/sda1、/dev/sdb
//
// 卷组（Volume Group, VG）：
//   一个或多个 PV 组成的存储池
//   例：VG "vg_data" = PV(/dev/sda1) + PV(/dev/sdb)
//
// 逻辑卷（Logical Volume, LV）：
//   从 VG 中划分出来的"虚拟分区"
//   例：LV "lv_root" 从 vg_data 划了 50G
//
// 物理块（Physical Extent, PE）：
//   PV 的最小分配单元，默认 4MB
//
// 逻辑块（Logical Extent, LE）：
//   LV 的最小分配单元，大小与 PE 相同
//
// 映射关系：
//   LV 由 LE 组成，LE → PE 的映射保存在 VG 的元数据区

// ============================================================
// 块大小常量
// ============================================================
#define PE_SIZE   4096    // Physical Extent = 4KB（简化版）
#define LE_SIZE   4096    // Logical Extent = 4KB（与 PE 相同）

// ============================================================
// 物理卷结构
// ============================================================
struct physical_volume {
    const char *name;          // 如 "sda1"
    struct block_device *dev;  // 底层块设备
    
    sector_t size;            // 总大小（扇区）
    sector_t pe_count;        // PE 数量
    
    // PE 位图（已分配 = 1，未分配 = 0）
    unsigned long *pe_bitmap;
    
    // 指向所属 VG
    struct volume_group *vg;
    int pv_index;             // 在 VG 中的索引
};

// 创建 PV
struct physical_volume *pv_create(const char *name, struct block_device *dev);

// 销毁 PV
void pv_destroy(struct physical_volume *pv);

// 从 PV 分配 n 个 PE
int pv_alloc_pe(struct physical_volume *pv, int count, int *out_pe_start);

// 释放 PE 范围
void pv_free_pe(struct physical_volume *pv, int pe_start, int count);

// 获取 PV 剩余可用 PE 数
int pv_free_pe_count(struct physical_volume *pv);

// ============================================================
// 卷组结构
// ============================================================
struct volume_group {
    const char *name;         // 如 "vg_data"
    
    // 成员 PV 列表
    struct physical_volume **pvs;
    int pv_count;
    int pv_capacity;          // 数组容量
    
    // 元数据区（保存 VG 的结构描述）
    sector_t metadata_start;   // 元数据区起始扇区
    sector_t metadata_size;    // 元数据区大小
    
    // 逻辑卷列表
    struct logical_volume **lvs;
    int lv_count;
    
    // 总 PE 数 / 可用 PE 数
    int total_pe;
    int free_pe;
};

// 创建 VG
struct volume_group *vg_create(const char *name);

// 向 VG 添加 PV
int vg_add_pv(struct volume_group *vg, struct physical_volume *pv);

// 从 VG 移除 PV（必须先迁移数据）
int vg_remove_pv(struct volume_group *vg, struct physical_volume *pv);

// 从 VG 分配 n 个 PE（跨 PV 的贪婪算法）
int vg_alloc_pe(struct volume_group *vg, int count,
                int *out_pv_index, int *out_pe_start);

// 扩大 VG（添加新 PV 后调用）
void vg_extend(struct volume_group *vg);

// ============================================================
// 逻辑卷结构
// ============================================================
struct logical_volume {
    const char *name;         // 如 "lv_root"
    const char *vg_name;     // 所属 VG 名
    
    sector_t size;           // 逻辑大小（扇区）
    int le_count;            // LE 数量
    
    // LE → PE 映射表
    // 每个 LE 对应一个 (pv_index, pe_start) 记录
    struct le_map {
        int pv_index;         // 落在哪个 PV 上
        int pe_start;        // 该 PV 上的起始 PE 号
    } *le_map;
    
    // 快照卷
    struct logical_volume *origin;   // 快照的原始卷
    sector_t snap_size;              // 快照卷大小
    unsigned long *cow_bitmap;        // Copy-On-Write 位图
    
    struct volume_group *vg;
};

// 创建 LV（从 VG 分配）
struct logical_volume *lv_create(struct volume_group *vg,
                                  const char *name, sector_t size);

// 扩容 LV
int lv_extend(struct logical_volume *lv, sector_t additional_size);

// 缩容 LV
int lv_shrink(struct logical_volume *lv, sector_t new_size);

// 创建快照卷
struct logical_volume *lv_create_snapshot(struct volume_group *vg,
                                           const char *snap_name,
                                           struct logical_volume *origin,
                                           sector_t snap_size);

// 销毁 LV
void lv_destroy(struct logical_volume *lv);

// ============================================================
// 核心映射函数（演示 LVM 的"虚拟地址翻译"）
// ============================================================

// 将 LV 内的逻辑扇区翻译成 (PV 设备号, 物理扇区)
// 相当于内存管理中的"虚拟地址翻译"
//
// 逻辑卷地址（LVA）→ 物理卷地址（PVA）
//
// lv_internal_sector: LV 内的扇区偏移（0 ~ lv->size-1）
// out_pv: 输出：目标 PV
// out_physical_sector: 输出：在 PV 内的物理扇区
void lv_map_sector(struct logical_volume *lv, sector_t lv_internal_sector,
                   struct physical_volume **out_pv,
                   sector_t *out_physical_sector);

// 快照 COW 写入：
// 写入快照时，如果该块还没 COW，先把原始块的当前内容复制到快照空间
int lv_snapshot_write(struct logical_volume *snap,
                      sector_t lv_sector,
                      const uint8_t *data, int len);

#endif // LVM_H
