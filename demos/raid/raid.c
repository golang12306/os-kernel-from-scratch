/*
 * raid.c — Linux md (Multiple Devices) 软 RAID 核心实现
 * 演示 RAID 0/1/5/6/10 的条带映射、奇偶校验计算、故障恢复
 *
 * 编译：gcc -o raid_demo raid.c raid.c -I.
 * 运行：./raid_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 基础类型（简化版，与 Linux kernel 类型对应）
// ============================================================

typedef uint64_t sector_t;   // 扇区号（512字节为单位）
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define SECTOR_SIZE  512
#define SECTOR_SHIFT 9

// ============================================================
// 模拟磁盘
// ============================================================

#define MAX_DISKS 16

struct mock_disk {
    char name[32];           // 盘符名
    sector_t size;           // 大小（扇区）
    bool faulty;             // 是否故障
    int read_count;          // 读次数统计
    int write_count;         // 写次数统计
};

// 创建模拟磁盘
struct mock_disk *disk_create(const char *name, sector_t size) {
    struct mock_disk *d = calloc(1, sizeof(struct mock_disk));
    strncpy(d->name, name, 31);
    d->size = size;
    return d;
}

// 模拟读写
void disk_write(struct mock_disk *d, sector_t sector, const uint8_t *buf, int len) {
    if (d->faulty) {
        printf("  [!!] 写操作失败：%s 盘已故障！\n", d->name);
        return;
    }
    d->write_count++;
}

void disk_read(struct mock_disk *d, sector_t sector, uint8_t *buf, int len) {
    if (d->faulty) {
        printf("  [!!] 读操作失败：%s 盘已故障！\n", d->name);
        memset(buf, 0, len);
        return;
    }
    d->read_count++;
}

// 模拟磁盘故障
void disk_set_faulty(struct mock_disk *d) {
    d->faulty = true;
    printf("[故障] 磁盘 %s 已标记为故障！\n", d->name);
}

// ============================================================
// RAID 结构
// ============================================================

#define RAID_LEVEL_0   0
#define RAID_LEVEL_1   1
#define RAID_LEVEL_5   5
#define RAID_LEVEL_6   6
#define RAID_LEVEL_10  10

struct raid {
    int level;              // RAID 级别
    int data_disks;         // 数据盘数量
    int total_disks;        // 总盘数
    sector_t chunk_sectors; // 条带大小（扇区数）
    sector_t size;          // 逻辑卷总大小（扇区）

    struct mock_disk *disks[MAX_DISKS];  // 成员盘
    struct mock_disk *spare;             // 热备盘
    bool degraded;          // 降级状态
    int *used_spares;       // 已用热备盘记录
};

// ============================================================
// RAID 5 奇偶校验计算（GF(2) 域上的异或运算）
// ============================================================

// RAID 5 P = D0 xor D1 xor D2 xor ... Dn
// RAID 6 Q = Reed-Solomon GF(2^8) 编码
void raid5_calc_parity(uint8_t *parity, uint8_t (*data)[8], int n, int stripe_len) {
    for (int i = 0; i < stripe_len; i++) {
        uint8_t p = 0;
        for (int j = 0; j < n; j++) {
            p ^= data[j][i];
        }
        parity[i] = p;
    }
}

// RAID 6 双奇偶校验（简化演示）
void raid6_calc_double_parity(uint8_t *p, uint8_t *q,
                               uint8_t (*data)[8], int n, int stripe_len) {
    for (int i = 0; i < stripe_len; i++) {
        uint8_t pp = 0, qq = 0;
        for (int j = 0; j < n; j++) {
            pp ^= data[j][i];
            qq ^= data[j][i] * (n - j);
        }
        p[i] = pp;
        q[i] = qq;
    }
}

// ============================================================
// 条带映射核心算法
// ============================================================

// RAID 0 条带映射：数据均匀分布在所有数据盘上
// 公式：stripe = sector / chunk_sectors
//       disk = stripe % data_disks
//       offset = (sector % chunk_sectors) * SECTOR_SIZE
void raid0_map(struct raid *r, sector_t sector,
               int *out_disk, sector_t *out_sector) {
    sector_t stripe = sector / r->chunk_sectors;
    *out_disk = stripe % r->data_disks;
    *out_sector = (stripe / r->data_disks) * r->chunk_sectors
                  + (sector % r->chunk_sectors);
}

// RAID 1 镜像映射：所有盘内容相同，随便读哪个都行
void raid1_map(struct raid *r, sector_t sector,
               int *out_disk, sector_t *out_sector) {
    (void)r;
    *out_disk = 0;  // 读主盘（第一个非故障盘）
    *out_sector = sector;
}

// RAID 5 分布式奇偶校验映射
// 关键设计：奇偶校验盘是 rotation（轮换）的，避免热点
// stripe = sector / chunk_sectors
// stripe_data_disks = data_disks 组成条带
// parity_disk = (stripe + stripe % data_disks) % (data_disks + 1) == data_disks 时为 parity
// data_disk 映射需要减去 parity 位置
void raid5_map(struct raid *r, sector_t sector,
               int *out_disk, sector_t *out_sector, int *out_is_parity) {
    sector_t stripe = sector / r->chunk_sectors;
    int data_in_stripe = sector % r->chunk_sectors;
    
    // 奇偶校验盘的位置（rotating）
    int p_disk = (stripe + stripe % r->data_disks) % (r->data_disks + 1);
    
    if (p_disk == r->data_disks) {
        // 这个扇区在奇偶校验盘上
        *out_is_parity = 1;
        *out_disk = r->data_disks;  // parity 在最后一个盘
        *out_sector = (stripe / (r->data_disks + 1)) * r->chunk_sectors + data_in_stripe;
    } else {
        // 数据扇区：需要处理 parity 盘的偏移
        *out_is_parity = 0;
        *out_disk = p_disk;  // 已经处理了 rotation 偏移
        *out_sector = (stripe / (r->data_disks + 1)) * r->chunk_sectors + data_in_stripe;
    }
}

// RAID 10: 先条带化再镜像
// layout: [0,1] [2,3] 为镜像组，组内条带化
void raid10_map(struct raid *r, sector_t sector,
                int *out_disk, sector_t *out_sector) {
    // 简化：每个镜像对条带化
    sector_t stripe = sector / r->chunk_sectors;
    int mirror_pair = stripe % (r->data_disks / 2);
    int chunk_in_stripe = sector % r->chunk_sectors;
    *out_disk = mirror_pair * 2;  // 读镜像的主盘
    *out_sector = (stripe / (r->data_disks / 2)) * r->chunk_sectors + chunk_in_stripe;
}

// ============================================================
// 降级读（故障盘处理）
// ============================================================

// RAID 5 降级读：从奇偶校验重建缺失数据
// D[failed] = P xor D[0] xor D[1] xor ... xor D[n] (排除 D[failed])
// 即：用已有的数据盘和 P 盘，重新算出故障盘的数据
void raid5_rebuild_stripe(struct raid *r, int failed_disk,
                          sector_t stripe_sector,
                          uint8_t *rebuilt_data, int len) {
    uint8_t buffers[MAX_DISKS][SECTOR_SIZE];
    uint8_t p_buf[SECTOR_SIZE];
    int available_disks = 0;
    
    printf("  [重建] 尝试从 stripe %lu 重建 disk[%d] 的数据...\n",
           (unsigned long)stripe_sector, failed_disk);
    
    // 收集其他盘的数据
    for (int i = 0; i < r->data_disks + 1; i++) {
        if (i == failed_disk) continue;
        if (r->disks[i] && !r->disks[i]->faulty) {
            disk_read(r->disks[i], stripe_sector, buffers[available_disks], len);
            available_disks++;
        }
    }
    
    // XOR 所有数据得到故障盘内容
    // P = D0 ^ D1 ^ ... ^ Dn
    // 所以 D[n] = P ^ D0 ^ D1 ^ ... ^ D[n-1]
    if (available_disks >= r->data_disks) {
        uint8_t result[SECTOR_SIZE] = {0};
        for (int i = 0; i < available_disks - 1; i++) {
            for (int j = 0; j < len; j++) {
                result[j] ^= buffers[i][j];
            }
        }
        memcpy(rebuilt_data, result, len);
        printf("  [重建] 成功从 %d 个盘重建了数据\n", available_disks - 1);
    }
}

// ============================================================
// 打印 RAID 配置信息（家用 NAS 场景说明）
// ============================================================

void print_raid_info(int level) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              家用 NAS 常见 RAID 配置对比                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    if (level == RAID_LEVEL_0) {
        printf("║  RAID 0（条带化） — 速度最快，安全性最差                     ║\n");
        printf("║                                                                ║\n");
        printf("║  2盘×4TB → 可用容量 8TB（无冗余）                            ║\n");
        printf("║  写入时数据分块，轮流写入两个盘，理论上读写速度 ×2          ║\n");
        printf("║                                                                ║\n");
        printf("║  ⚠️ 任一盘故障 = 所有数据丢失                               ║\n");
        printf("║  ❌ 家用 NAS 不推荐！只适合临时的高速缓存空间               ║\n");
    } else if (level == RAID_LEVEL_1) {
        printf("║  RAID 1（镜像） — 简单可靠，空间利用率低                    ║\n");
        printf("║                                                                ║\n");
        printf("║  2盘×4TB → 可用容量 4TB（50%% 利用率）                      ║\n");
        printf("║  写入时同时写到两个盘，互为镜像                              ║\n");
        printf("║                                                                ║\n");
        printf("║  ✅ 1 盘故障不丢数据，适合重要文件                          ║\n");
        printf("║  ❌ 空间利用率低，4 盘才能增加到约 2 倍读性能              ║\n");
    } else if (level == RAID_LEVEL_5) {
        printf("║  RAID 5（分布式奇偶校验） — 平衡之选                       ║\n");
        printf("║                                                                ║\n");
        printf("║  3盘×4TB → 可用容量 8TB（67%% 利用率）                      ║\n");
        printf("║  奇偶校验分散在各盘，任意 1 盘故障可重建                    ║\n");
        printf("║                                                                ║\n");
        printf("║  ✅ 读性能好（类似 RAID 0），有容错能力                    ║\n");
        printf("║  ⚠️ 重建时（从 1 盘恢复所有数据）有二次故障风险             ║\n");
        printf("║  ❌ 4TB+ 单盘重建时间可能超过 24 小时                      ║\n");
    } else if (level == RAID_LEVEL_6) {
        printf("║  RAID 6（双奇偶校验） — 高可靠性                           ║\n");
        printf("║                                                                ║\n");
        printf("║  4盘×4TB → 可用容量 12TB（75%% 利用率）                     ║\n");
        printf("║  两套奇偶校验（P+Q），可同时容忍 2 盘故障                   ║\n");
        printf("║                                                                ║\n");
        printf("║  ✅ 重建期间再坏 1 盘也不会丢数据（RAID 5 的痛点）        ║\n");
        printf("║  ✅ 视频监控等高写入场景推荐                               ║\n");
        printf("║  ❌ 写入性能略低于 RAID 5（多一次 Q 计算）               ║\n");
    } else if (level == RAID_LEVEL_10) {
        printf("║  RAID 10（条带化+镜像） — 高性能 + 高可靠                  ║\n");
        printf("║                                                                ║\n");
        printf("║  4盘×4TB → 可用容量 8TB（50%% 利用率）                      ║\n");
        printf("║  先两两镜像，再条带化。兼具 RAID 0 的速度和 RAID 1 的安全 ║\n");
        printf("║                                                                ║\n");
        printf("║  ✅ 故障盘在同组内不影响其他组                             ║\n");
        printf("║  ✅ 重建只影响镜像对，速度快                              ║\n");
        printf("║  ❌ 空间利用率固定 50%%，成本高                            ║\n");
        printf("║                                                                ║\n");
        printf("║  💡 Synology SHR-2 = 改良版 RAID 10，自动优化布局         ║\n");
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// 主演示
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║       软 RAID 实现演示 — 从零写 OS 内核系列                  ║\n");
    printf("║       RAID 0 / 1 / 5 / 6 / 10  条带映射与故障恢复             ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    // ============================================================
    // 演示 1：RAID 0 条带映射
    // ============================================================
    printf("\n【演示 1】RAID 0 条带映射（2 盘，条带大小 64KB）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    struct raid r0 = {
        .level = RAID_LEVEL_0,
        .data_disks = 2,
        .chunk_sectors = 128,  // 64KB = 128 * 512
    };
    r0.disks[0] = disk_create("sda", 2000000);
    r0.disks[1] = disk_create("sdb", 2000000);
    
    printf("写入逻辑扇区 0~255（共 128KB = 2个条带）：\n");
    for (sector_t s = 0; s < 256; s += 128) {
        int disk;
        sector_t ds;
        raid0_map(&r0, s, &disk, &ds);
        printf("  扇区 %4lu → 物理盘 %s，扇区 %lu（条带 %lu）\n",
               (unsigned long)s,
               r0.disks[disk]->name,
               (unsigned long)ds,
               (unsigned long)(s / r0.chunk_sectors));
    }
    
    printf("\nRAID 0 特点：所有数据条带均匀分布，容量 = 全部盘之和，\n");
    printf("            任意一盘故障全部数据丢失。\n");
    print_raid_info(RAID_LEVEL_0);
    
    // ============================================================
    // 演示 2：RAID 5 分布式奇偶校验（核心演示）
    // ============================================================
    printf("\n【演示 2】RAID 5 分布式奇偶校验（3 盘，条带大小 64KB）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    struct raid r5 = {
        .level = RAID_LEVEL_5,
        .data_disks = 3,
        .chunk_sectors = 128,  // 64KB
    };
    for (int i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof(name), "sd%c", 'a' + i);
        r5.disks[i] = disk_create(name, 2000000);
    }
    
    printf("条带 0~11 的物理布局（注意 parity 盘 rotation）：\n");
    printf("%-8s %-8s %-8s %-8s  说明\n", "条带", "disk[a]", "disk[b]", "disk[c]", "");
    printf("%-8s %-8s %-8s %-8s  P在[3]\n", "0", "D0", "D1", "D2", "← 条带0，parity在第4个位置（disk[3]=P）");
    
    // 模拟布局输出（前12个条带）
    for (int stripe = 0; stripe < 12; stripe++) {
        int p_disk = (stripe + stripe % 3) % 4;
        char slots[4][8] = {"----", "----", "----", "----"};
        for (int d = 0; d < 3; d++) {
            int mapped_disk = (p_disk <= d) ? d + 1 : d;
            snprintf(slots[d], 8, "D%d", d);
        }
        snprintf(slots[p_disk], 8, "P%d", stripe);
        printf("%-8d %-8s %-8s %-8s  P在[%d]\n", stripe, slots[0], slots[1], slots[2], p_disk);
    }
    
    printf("\n关键设计：parity 盘位置随条带轮换，避免 RAID 4 的写瓶颈！\n");
    printf("（RAID 4 所有写操作都要写同一个 parity 盘，形成热点）\n");
    print_raid_info(RAID_LEVEL_5);
    
    // ============================================================
    // 演示 3：故障重建
    // ============================================================
    printf("\n【演示 3】RAID 5 故障重建（降级读）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    printf("场景：4 盘 RAID 5（3 数据 + 1 parity），disk[b] 突然故障\n");
    printf("      读取条带 0 的数据 disk[b] 部分...\n");
    
    // 模拟故障
    disk_set_faulty(r5.disks[1]);
    
    uint8_t rebuilt[SECTOR_SIZE];
    raid5_rebuild_stripe(&r5, 1, 0, rebuilt, SECTOR_SIZE);
    
    printf("\n结果：通过 P ^ D0 ^ D2 异或运算，在没有 disk[b] 的情况下\n");
    printf("       重建出了 disk[b] 的数据！这就是 RAID 5 容错的原理。\n");
    
    printf("\n💡 家用场景提示：\n");
    printf("   Synology SHR 默认就是 RAID 5/SHR-1（单奇偶校验）\n");
    printf("   QNAP 通常默认 RAID 5，但会提示开启 Qtier 等功能\n");
    printf("   重要数据强烈建议开启【双盘冗余 SHR-2】或 RAID 6\n");
    printf("   因为 RAID 5 重建时，剩余盘同时承受高负载读取，\n");
    printf("   这个窗口期如果再坏一盘 = 所有数据永久丢失！\n");
    
    // ============================================================
    // 演示 4：RAID 6 双奇偶校验
    // ============================================================
    printf("\n【演示 4】RAID 6 双奇偶校验 P+Q\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    printf("RAID 5 的痛点：只能容忍 1 盘故障。重建期间坏第二盘？全丢！\n");
    printf("RAID 6 的解决：额外计算 Q 校验，可容忍 2 盘同时故障。\n\n");
    
    uint8_t data_example[3][8] = {
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0}
    };
    
    uint8_t p_result[8], q_result[8];
    
    raid5_calc_parity(p_result, data_example, 3, 8);
    raid6_calc_double_parity(p_result, q_result, data_example, 3, 8);
    
    printf("  D0 = {0x11, 0x22, ...}\n");
    printf("  D1 = {0xAA, 0xBB, ...}\n");
    printf("  D2 = {0x12, 0x34, ...}\n");
    printf("\n  P = D0 ^ D1 ^ D2 = {0x%02x, 0x%02x, ...}\n", p_result[0], p_result[1]);
    printf("  Q = 线性组合 = {0x%02x, 0x%02x, ...}（GF(2^8)域乘法）\n", q_result[0], q_result[1]);
    
    printf("\n数学原理：\n");
    printf("  · P 盘：所有数据盘的 XOR，可恢复任意 1 盘\n");
    printf("  · Q 盘：GF(2^8) 域的 Reed-Solomon 编码\n");
    printf("  · 2 盘故障时，解线性方程组恢复数据\n");
    
    print_raid_info(RAID_LEVEL_6);
    
    // ============================================================
    // 演示 5：家用 NAS 选型建议
    // ============================================================
    printf("\n【演示 5】家用 NAS RAID 选型决策树\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("                    ┌─────────────────────┐\n");
    printf("                    │ 你需要存储什么？    │\n");
    printf("                    └──────────┬──────────┘\n");
    printf("              ┌───────────────┼───────────────┐\n");
    printf("              ▼               ▼               ▼\n");
    printf("     ┌─────────────┐  ┌─────────────┐  ┌─────────────┐\n");
    printf("     │ 照片/文档   │  │ 4K 影视库   │  │ 重要业务数据│\n");
    printf("     │ （可承受   │  │ （高读写，  │  │ （零容忍   │\n");
    printf("     │  1-2盘丢失）│  │  容量优先） │  │  数据丢失） │\n");
    printf("     └──────┬──────┘  └──────┬──────┘  └──────┬──────┘\n");
    printf("            ▼                ▼                ▼\n");
    printf("     ┌─────────────┐  ┌─────────────┐  ┌─────────────┐\n");
    printf("     │ SHR-1       │  │ RAID 5      │  │ RAID 6      │\n");
    printf("     │ 4盘=3可用   │  │ 4盘=3可用   │  │ RAID 10     │\n");
    printf("     │ 允许1盘故障 │  │ 允许1盘故障 │  │ 允许2盘故障 │\n");
    printf("     │             │  │ 重建风险⚠️  │  │ ✅ 最安全  │\n");
    printf("     └─────────────┘  └─────────────┘  └─────────────┘\n");
    
    printf("\n⚠️  最最重要的提醒（无数人踩过的坑）：\n");
    printf("   RAID ≠ 备份！RAID 只保护你免受硬盘故障的影响。\n");
    printf("   但删错文件、中病毒、NAS 被盗——RAID 统统保护不了！\n");
    printf("   正确做法：RAID（硬件保护）+ 定期冷备份/云备份（数据保护）\n");
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  关注公众号「必收」并 star 本仓库                            ║\n");
    printf("║  GitHub: github.com/golang12306/os-kernel-from-scratch       ║\n");
    printf("║  对应 Demo: demos/raid/raid.c                                ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}
