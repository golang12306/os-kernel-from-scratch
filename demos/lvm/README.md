# LVM 演示 — 从零写 OS 内核系列

对应文章：["服务器硬盘满了不删数据，我选择动态扩容——LVM 原理"](https://github.com/golang12306/os-kernel-from-scratch)

## 编译 & 运行

```bash
cd demos/lvm/
gcc -o lvm_demo lvm.c && ./lvm_demo
```

## 演示内容

1. **PV/VG/LV 三层架构** — 物理卷 → 卷组 → 逻辑卷的层次关系
2. **在线扩容** — 动态添加 PV，扩容 LV 过程不中断业务
3. **快照卷 COW** — Copy-On-Write 机制，备份不停服务
4. **LV 条带化** — 跨盘条带分布，提升读写性能

## 核心概念

| 概念 | 简写 | 说明 |
|------|------|------|
| Physical Extent | PE | PV 的最小分配单元（默认 4MB） |
| Logical Extent | LE | LV 的最小分配单元，与 PE 大小相同 |
| Physical Volume | PV | 物理磁盘或分区 |
| Volume Group | VG | 多个 PV 组成的存储池 |
| Logical Volume | LV | 从 VG 划分的虚拟分区 |

## 快照 COW 原理

1. 快照卷创建时，与原始卷共享所有 LE（0 复制）
2. 第一次写入原始卷某个块时，先把原始数据复制到 COW 区域
3. 后续写入直接写入原始位置
4. 快照读取时：COW 位图=1 的块读 COW 区域，否则读原始卷
