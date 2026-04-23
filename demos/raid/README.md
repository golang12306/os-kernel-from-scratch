# RAID 演示 — 从零写 OS 内核系列

对应文章：["花2000块买NAS，结果RAID配错了，数据全丢了"](https://github.com/golang12306/os-kernel-from-scratch)

## 目录结构

```
raid/
├── raid.h      # RAID 结构体定义、接口声明
├── raid.c      # 完整可运行的 RAID 演示程序
└── README.md   # 本文件
```

## 编译 & 运行

```bash
cd demos/raid/
gcc -o raid_demo raid.c
./raid_demo
```

## 演示内容

1. **RAID 0 条带映射** — 2 盘条带化，数据均匀分布的映射算法
2. **RAID 5 分布式奇偶校验** — rotating parity 避免热点盘的数学原理
3. **RAID 5 故障重建** — 通过 XOR 运算重建故障盘数据（降级读）
4. **RAID 6 双奇偶校验 P+Q** — GF(2^8) 域编码，可容忍 2 盘故障
5. **家用 NAS RAID 选型** — Synology SHR / QNAP RAID 5/6 的实际建议

## 核心算法

### RAID 5 条带映射

```c
// parity 盘位置随条带轮换（rotating）
int p_disk = (stripe + stripe % data_disks) % (data_disks + 1);
```

这就是为什么 RAID 5 不会像 RAID 4 那样出现"奇偶校验盘热点"问题。

### RAID 5 故障重建

```
D[failed] = P ^ D[0] ^ D[1] ^ ... ^ D[n]  (排除故障盘)
```

RAID 5 的容错核心：所有盘的 XOR 等于 0，所以知道 P 和其他数据盘，就能反推出任何一盘的数据。

## 家用 NAS 参考

| 级别 | 4×4TB 可用 | 容错 | 重建风险 |
|------|-----------|------|---------|
| SHR-1 (RAID 5) | 12TB | 1 盘 | ⚠️ 重建期间危险 |
| SHR-2 (RAID 6) | 8TB | 2 盘 | ✅ 安全 |
| RAID 10 | 8TB | 半数盘 | ✅ 重建快 |

> ⚠️ **重要提醒**：RAID ≠ 备份。RAID 只保护硬盘故障，不保护误删、病毒或盗窃。
