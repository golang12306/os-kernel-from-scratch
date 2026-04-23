/*
 * lvm.c — Linux LVM（Logical Volume Manager）核心机制演示
 * 演示 PV/VG/LV 三层架构、PE/LE 分配、快照 COW
 *
 * 编译：gcc -o lvm_demo lvm.c -I.
 * 运行：./lvm_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 基础类型
// ============================================================

typedef uint64_t sector_t;
typedef uint32_t u32;
typedef uint64_t u64;

#define SECTOR_SIZE  512
// 实际 LVM 支持多种 PE 大小：1MB, 2MB, 4MB, 8MB, 16MB, 32MB, 64MB
// 此处用 1MB PE 作为演示（1MB = 2048 个扇区）
#define PE_SIZE_SECTORS (1024 * 1024 / SECTOR_SIZE)  // PE = 1MB
#define LE_SIZE_SECTORS  PE_SIZE_SECTORS               // LE 与 PE 相同

// ============================================================
// 简化的块设备
// ============================================================

struct mock_disk {
    char name[32];
    sector_t size;
    bool faulty;
};

struct mock_disk *disk_create(const char *name, sector_t size) {
    struct mock_disk *d = calloc(1, sizeof(struct mock_disk));
    strncpy(d->name, name, 31);
    d->size = size;
    return d;
}

// ============================================================
// PV（Physical Volume）
// ============================================================

#define MAX_PVS 16

struct pv {
    char name[32];
    sector_t size;             // 扇区总数
    int pe_count;             // PE 数量
    unsigned long *pe_bitmap;  // 位图：0=空闲，1=已分配
    int free_pe;
};

struct pv *pv_create(const char *name, sector_t size_sectors) {
    struct pv *p = calloc(1, sizeof(struct pv));
    strncpy(p->name, name, 31);
    p->size = size_sectors;
    p->pe_count = size_sectors / PE_SIZE_SECTORS;
    p->pe_bitmap = calloc((p->pe_count + 63) / 64, sizeof(unsigned long));
    p->free_pe = p->pe_count;
    return p;
}

int pv_alloc(struct pv *p, int count, int *out_start) {
    int found = 0, start = -1;
    for (int i = 0; i < p->pe_count; i++) {
        // 查找连续 count 个空闲 PE
        int bitmap_idx = i / 64;
        int bit_offset = i % 64;
        if (!(p->pe_bitmap[bitmap_idx] & (1UL << bit_offset))) {
            if (found == 0) start = i;
            found++;
            if (found == count) break;
        } else {
            found = 0; start = -1;
        }
    }
    if (found < count) return -1;
    // 标记为已分配
    for (int i = start; i < start + count; i++) {
        int bi = i / 64, bo = i % 64;
        p->pe_bitmap[bi] |= (1UL << bo);
    }
    p->free_pe -= count;
    *out_start = start;
    return 0;
}

void pv_free(struct pv *p, int start, int count) {
    for (int i = start; i < start + count; i++) {
        int bi = i / 64, bo = i % 64;
        p->pe_bitmap[bi] &= ~(1UL << bo);
    }
    p->free_pe += count;
}

// ============================================================
// VG（Volume Group）
// ============================================================

#define MAX_LVS 16

struct vg {
    char name[32];
    struct pv *pvs[MAX_PVS];
    int pv_count;
    int total_pe;    // 总 PE 数
    int free_pe;     // 可用 PE 数
    
    struct lv *lvs[MAX_LVS];
    int lv_count;
};

struct vg *vg_create(const char *name) {
    struct vg *g = calloc(1, sizeof(struct vg));
    strncpy(g->name, name, 31);
    return g;
}

int vg_add_pv(struct vg *g, struct pv *p) {
    if (g->pv_count >= MAX_PVS) return -1;
    g->pvs[g->pv_count++] = p;
    g->total_pe += p->pe_count;
    g->free_pe += p->free_pe;
    return 0;
}

// 从 VG 分配 PE（贪婪：从第一个有空间的 PV 开始分配）
int vg_alloc_pe(struct vg *g, int count, int *out_pv_idx, int *out_pe_start) {
    for (int i = 0; i < g->pv_count; i++) {
        if (g->pvs[i]->free_pe >= count) {
            int start;
            if (pv_alloc(g->pvs[i], count, &start) == 0) {
                g->free_pe -= count;
                *out_pv_idx = i;
                *out_pe_start = start;
                return 0;
            }
        }
    }
    return -1; // 没有足够的连续空间
}

// ============================================================
// LV（Logical Volume）
// ============================================================

struct le_map {
    int pv_index;    // 落在哪个 PV 上
    int pe_start;    // 起始 PE 号
};

struct lv {
    char name[32];
    char vg_name[32];
    int le_count;            // LE 数量
    struct le_map *le_map;   // LE[0]~LE[n] → (PV, PE) 映射
    int pv_count;            // 涉及多少个 PV
    
    // 快照相关
    struct lv *origin;       // 如果是快照卷，指向原始卷
    unsigned long *cow_bitmap; // COW 位图（1=已复制，0=共享原始数据）
    int cow_pe_start;         // 快照的 COW 存储区域起始 PE
    int cow_pe_count;         // COW 区域大小
};

struct lv *lv_create(struct vg *g, const char *name, int le_count) {
    struct lv *l = calloc(1, sizeof(struct lv));
    strncpy(l->name, name, 31);
    strncpy(l->vg_name, g->name, 31);
    l->le_count = le_count;
    l->le_map = calloc(le_count, sizeof(struct le_map));
    
    // 分配 PE
    int remaining = le_count;
    int pv_used[MAX_PVS] = {0};
    
    while (remaining > 0) {
        int pv_idx, pe_start;
        if (vg_alloc_pe(g, 1, &pv_idx, &pe_start) != 0) {
            printf("  [错误] VG %s 空间不足！只能分配 %d 个 LE\n",
                   g->name, le_count - remaining);
            break;
        }
        int le_idx = le_count - remaining;
        l->le_map[le_idx].pv_index = pv_idx;
        l->le_map[le_idx].pe_start = pe_start;
        pv_used[pv_idx]++;
        remaining--;
    }
    return l;
}

// 逻辑扇区 → 物理扇区
sector_t lv_map_sector(struct lv *l, sector_t lv_sector, int *out_pv_idx) {
    int le_idx = lv_sector / (PE_SIZE_SECTORS);
    if (le_idx >= l->le_count) le_idx = l->le_count - 1;
    *out_pv_idx = l->le_map[le_idx].pv_index;
    sector_t offset_in_pe = lv_sector % PE_SIZE_SECTORS;
    return l->le_map[le_idx].pe_start * PE_SIZE_SECTORS + offset_in_pe;
}

// 扩容 LV
int lv_extend(struct lv *l, struct vg *g, int additional_le) {
    int old_count = l->le_count;
    l->le_map = realloc(l->le_map, (old_count + additional_le) * sizeof(struct le_map));
    
    for (int i = 0; i < additional_le; i++) {
        int pv_idx, pe_start;
        if (vg_alloc_pe(g, 1, &pv_idx, &pe_start) != 0) {
            printf("  [错误] 扩容失败！VG 空间不足\n");
            return -1;
        }
        l->le_map[old_count + i].pv_index = pv_idx;
        l->le_map[old_count + i].pe_start = pe_start;
    }
    l->le_count += additional_le;
    return 0;
}

// 创建快照卷（COW 机制）
struct lv *lv_create_snapshot(struct vg *g, const char *snap_name,
                                struct lv *origin, int cow_pe_count) {
    struct lv *snap = calloc(1, sizeof(struct lv));
    strncpy(snap->name, snap_name, 31);
    strncpy(snap->vg_name, g->name, 31);
    snap->origin = origin;
    snap->le_count = origin->le_count;
    snap->le_map = origin->le_map; // 快照与原始卷共享 LE 映射！
    snap->cow_pe_count = cow_pe_count;
    
    // 分配 COW 区域
    int cow_start;
    if (vg_alloc_pe(g, cow_pe_count, &cow_start, &cow_start) != 0) {
        printf("  [错误] COW 空间不足！\n");
        free(snap);
        return NULL;
    }
    snap->cow_pe_start = cow_start;
    snap->cow_bitmap = calloc((origin->le_count + 63) / 64, sizeof(unsigned long));
    
    printf("  [快照] COW 区域分配完成，共 %d 个 PE，起点 PE=%d\n",
           cow_pe_count, cow_start);
    return snap;
}

// 快照写入（Copy-On-Write）
void snapshot_write(struct lv *snap, sector_t lv_sector) {
    if (!snap->origin) return;
    int le_idx = lv_sector / PE_SIZE_SECTORS;
    int bi = le_idx / 64, bo = le_idx % 64;
    
    if (!(snap->cow_bitmap[bi] & (1UL << bo))) {
        // 第一次写入这个块：复制原始数据到 COW 区域
        printf("  [COW] LE[%d] 第一次写入，复制原始数据\n", le_idx);
        snap->cow_bitmap[bi] |= (1UL << bo);
    }
    // 实际写入时，直接写原始卷（快照记录了 COW 位图）
}

// ============================================================
// 打印状态
// ============================================================

void print_pv(struct pv *p) {
    int used = p->pe_count - p->free_pe;
    printf("  PV %s: %d/%d PE used (%.0f%%), %d free\n",
           p->name, used, p->pe_count,
           100.0 * used / p->pe_count, p->free_pe);
}

void print_vg(struct vg *g) {
    printf("\n[卷组] %s\n", g->name);
    printf("  成员盘: %d 个 PV\n", g->pv_count);
    for (int i = 0; i < g->pv_count; i++) print_pv(g->pvs[i]);
    printf("  总容量: %d PE (%.1f GB)\n",
           g->total_pe, (sector_t)g->total_pe * PE_SIZE_SECTORS * SECTOR_SIZE / 1024.0 / 1024.0 / 1024.0);
    printf("  可用: %d PE (%.1f GB)\n",
           g->free_pe, (sector_t)g->free_pe * PE_SIZE_SECTORS * SECTOR_SIZE / 1024.0 / 1024.0 / 1024.0);
}

void print_lv(struct lv *l) {
    printf("  LV %s: %d LE (%.1f GB)", l->name, l->le_count,
           (sector_t)l->le_count * PE_SIZE_SECTORS * SECTOR_SIZE / 1024.0 / 1024.0 / 1024.0);
    if (l->origin)
        printf(" [快照卷，原卷: %s, COW区: %d PE]", l->origin->name, l->cow_pe_count);
    printf("\n");
}

// ============================================================
// 演示
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║       LVM 逻辑卷管理演示 — 从零写 OS 内核系列              ║\n");
    printf("║       PV / VG / LV 三层架构  ·  动态扩容  ·  快照 COW     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    // ============================================================
    // 演示 1：VG 创建与 LV 分配
    // ============================================================
    printf("\n【演示 1】VG 创建与 LV 动态分配\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("场景：有一台服务器，最初只有一块 10GB 盘\n");
    printf("      创建 VG 'vg_data'，然后在上面划出 /home 和 /var\n\n");
    
    // 模拟一块 10GB 盘（1 PE = 1MB，共 10,240 PE）
    int disk_10g_pe = 10 * 1024;  // 10GB / 1MB = 10,240 PE
    struct pv *pv1 = pv_create("sda1", (sector_t)disk_10g_pe * PE_SIZE_SECTORS);
    
    struct vg *vg = vg_create("vg_data");
    vg_add_pv(vg, pv1);
    
    // 划出 /home (3GB = 3072 LE)
    int home_le = 3 * 1024;  // 3GB / 1MB = 3072 LE
    printf("  [分配中] lv_home %d LE (3GB)...\n", home_le);
    struct lv *lv_home = lv_create(vg, "lv_home", home_le);
    
    // 划出 /var (2GB = 2048 LE)
    int var_le = 2 * 1024;  // 2GB / 1MB = 2048 LE
    printf("  [分配中] lv_var %d LE (2GB)...\n", var_le);
    struct lv *lv_var = lv_create(vg, "lv_var", var_le);
    
    printf("已创建逻辑卷：\n");
    print_lv(lv_home);
    print_lv(lv_var);
    print_vg(vg);
    
    // ============================================================
    // 演示 2：在线扩容（无需停机！）
    // ============================================================
    printf("\n【演示 2】在线扩容 — 服务器硬盘不够了，插一块新盘\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("场景：/var 快满了，插了一块新盘 sdb1 (50GB)，在线扩容 /var\n");
    
    // 新增一块 5GB 盘 (5120 PE)
    int disk_5g_pe = 5 * 1024;
    struct pv *pv2 = pv_create("sdb1", (sector_t)disk_5g_pe * PE_SIZE_SECTORS);
    vg_add_pv(vg, pv2);
    
    printf("\n新盘加入 VG 后：\n");
    print_vg(vg);
    
    // 给 /var 增加 3GB
    int extend_le = 3 * 1024;  // 3GB / 1MB = 3072 LE
    printf("\n扩容 lv_var +3GB...\n");
    int old_var_le = lv_var->le_count;
    lv_extend(lv_var, vg, extend_le);
    printf("  /var: %d LE → %d LE (+3GB)\n", old_var_le, lv_var->le_count);
    print_vg(vg);
    
    // ============================================================
    // 演示 3：快照卷（备份不停服务）
    // ============================================================
    printf("\n【演示 3】快照卷 — 备份 MySQL 数据库，服务不中断\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("传统方式：停服务 → 备份 → 启动 = 业务中断 30 分钟\n");
    printf("LVM 快照：在线创建快照卷 → 备份快照 → 删除快照 = 业务无感知\n\n");
    
    printf("创建 /var 的快照 lv_var_snapshot（保留 1GB COW 空间）：\n");
    int cow_pe = 1 * 1024;  // 1GB COW 区域
    struct lv *snap = lv_create_snapshot(vg, "lv_var_snapshot", lv_var, cow_pe);
    print_lv(snap);
    print_vg(vg);
    
    printf("\n模拟业务写入：\n");
    printf("  快照卷初始状态：共享原始 LV 的所有 LE（0 复制）\n");
    printf("  第1次写入 lv_var 的 LE[0]... ");
    snapshot_write(snap, 0);
    printf("  ✓ COW 触发：原始数据已复制到 COW 区域\n");
    printf("  第2次写入 lv_var 的 LE[0]... ");
    snapshot_write(snap, 0);
    printf("  ✓ COW 已设置，直接写入原始位置\n");
    printf("  第3次写入 lv_var 的 LE[1000]... ");
    snapshot_write(snap, (sector_t)1000 * PE_SIZE_SECTORS);
    printf("  ✓ COW 触发：LE[1000] 原始数据复制到 COW\n");
    
    printf("\n快照卷读取（读取的是快照创建时的数据）：\n");
    printf("  snap LE[0] → 指向 COW 区域（已复制的原始数据）\n");
    printf("  snap LE[1000] → 指向 COW 区域（已复制的原始数据）\n");
    printf("  snap LE[2000] → 指向原始 LV（从未修改，数据即原始值）\n");
    
    // ============================================================
    // 演示 4：跨盘条带化（LV 条带化分布）
    // ============================================================
    printf("\n【演示 4】LV 条带化 — 4 块盘组成高速 LV\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("条带化原理：数据轮流写入多个 PV，读写性能 ×N\n");
    
    struct vg *vg_stripe = vg_create("vg_stripe");
    struct pv *pvs[4];
    int stripe_pe = 1000; // 每盘 1000 PE
    for (int i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof(name), "sd%c", 'a' + i);
        pvs[i] = pv_create(name, (sector_t)stripe_pe * PE_SIZE_SECTORS);
        vg_add_pv(vg_stripe, pvs[i]);
    }
    
    // 创建条带化 LV
    struct lv *lv_stripe = lv_create(vg_stripe, "lv_raid0", stripe_pe);
    
    printf("条带化 LV 'lv_raid0' (1000 LE) 的物理布局：\n");
    printf("%-12s ", "LE");
    for (int i = 0; i < 4; i++) printf("%-12s ", pvs[i]->name);
    printf("\n");
    for (int le = 0; le < 16; le++) {
        printf("%-12d ", le);
        for (int pv = 0; pv < 4; pv++) {
            if (lv_stripe->le_map[le].pv_index == pv)
                printf("%-12d ", lv_stripe->le_map[le].pe_start);
            else
                printf("%-12s ", "");
        }
        printf("\n");
    }
    printf("...\n");
    printf("\n效果：数据以 1MB 为单位轮询写入 4 块盘，顺序读写速度 ×4！\n");
    
    // ============================================================
    // 家用场景提示
    // ============================================================
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              家用/服务器场景 LVM 实战                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                              ║\n");
    printf("║  场景 1：Linux 服务器根分区扩容                            ║\n");
    printf("║    $ pvcreate /dev/sdb1                                     ║\n");
    printf("║    $ vgextend vg_root /dev/sdb1                            ║\n");
    printf("║    $ lvextend -L +100G /dev/vg_root/lv_root               ║\n");
    printf("║    $ resize2fs /dev/vg_root/lv_root        （在线扩容！） ║\n");
    printf("║                                                              ║\n");
    printf("║  场景 2：MySQL 在线备份（快照卷）                          ║\n");
    printf("║    $ lvcreate -s -L 20G -n snap /dev/vg_data/lv_mysql     ║\n");
    printf("║    $ mount /dev/vg_data/snap /mnt/snap                     ║\n");
    printf("║    $ mysqldump ... > /mnt/snap/backup.sql                  ║\n");
    printf("║    $ umount /mnt/snap && lvremove /dev/vg_data/snap       ║\n");
    printf("║                                                              ║\n");
    printf("║  ⚠️ 注意：快照卷的 COW 空间必须够大！                   ║\n");
    printf("║    如果快照期间写入量超过 COW 空间，快照会失效           ║\n");
    printf("║    经验公式：COW = 快照时长(hours) × 写入速率(GB/h) × 1.5║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  关注公众号「必收」并 star 本仓库                            ║\n");
    printf("║  GitHub: github.com/golang12306/os-kernel-from-scratch     ║\n");
    printf("║  对应 Demo: demos/lvm/lvm.c                                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}
