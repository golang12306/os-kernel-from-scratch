# OS Kernel From Scratch · 从零写OS内核

<p align="center">
  <img src="https://img.shields.io/github/stars/golang12306/os-kernel-from-scratch?style=for-the-badge" alt="Stars">
  <img src="https://img.shields.io/github/forks/golang12306/os-kernel-from-scratch?style=for-the-badge" alt="Forks">
  <img src="https://img.shields.io/badge/articles-46+-orange?style=for-the-badge" alt="Articles">
  <img src="https://img.shields.io/badge/demos-221-blue?style=for-the-badge" alt="Demos">
</p>

<p align="center">
  <strong>从零理解操作系统内核 / Learn OS Internals from Zero</strong>
</p>

<p align="center">
  🔥 全网最完整的 Linux 内核原理学习项目 | 配套 46+ 篇公众号深度图文 | 221 个可运行 Demo
</p>

---

## 这是什么项目？

**OS Kernel From Scratch** 是一个操作系统内核与 Linux 内核原理的学习项目，包含两大部分：

| 分支 | 内容 |
|------|------|
| **wandos** | 真实可运行的 x86 OS 内核（C++ 实现），从零实现引导、分页、调度、文件系统 |
| **Linux 内核原理** | 221 个独立 Demo，覆盖内存管理、进程调度、文件系统、网络、容器、安全等核心子系统 |

> 📺 [公众号系列：从零写OS内核](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzAxMjQwMjA5NQ==&action=getalbum&album_id=4239918263758864395) · 46 篇深度图文持续更新

---

## 学习路线图

```
第一阶段：内核基础（已完结）
──────────────────────────────────────────────────
  引导 → 保护模式 → 分页 → GDT/LDT/IDT/TSS → 汇编基础
  └── wandos 内核：实现最小可运行系统

第二阶段：内核基础设施 ⭐ 强烈推荐入门
──────────────────────────────────────────────────
  vDSO · SMP多核 · MESI缓存一致性 · TSO内存顺序
  page frame · kmalloc · slab/slab allocator
  └── 理解CPU和内存的真实工作方式

第三阶段：文件系统（已完结）
──────────────────────────────────────────────────
  ext4日志 · inode · dentry · page cache · writeback
  VFS虚拟层 · bio块IO · 电梯调度(CFQ/noop/deadline) · raid · LVM
  └── 理解Linux存储子系统的全貌

第四阶段：网络（更新中）
──────────────────────────────────────────────────
  netfilter · iptables · conntrack · route
  socket套接字 · TCP三次握手/四次挥手 · UDP
  neigh ARP · TCP拥塞控制
  └── 理解数据包在内核里的完整旅程

第五阶段：进程与调度
──────────────────────────────────────────────────
  task_struct · CFS完全公平调度 · RT实时调度
  idle · loadavg · nice优先级 · coredump
  └── 理解进程管理和CPU调度的本质

第六阶段：内存管理
──────────────────────────────────────────────────
  zram/zswap压缩swap · hugepage/THP大页 · KSM去重
  memcg · oom score · vm pressure · memblock · CMA
  └── 理解Linux如何管理每一字节内存

第七阶段：安全与隔离
──────────────────────────────────────────────────
  capabilities · apparmor · landlock · seccomp
  smap/smep · kaslr · shadow stack · cet · spec_ctrl
  └── 理解现代Linux安全防护机制
```

---

## Demo 目录结构

```
demos/
├── 基础设施
│   ├── vDSO/
│   ├── SMP/
│   ├── MESI/
│   ├── TSO/
│   ├── page-frame/
│   ├── kmalloc/
│   ├── slab-allocator/
│   └── memory_barrier/
├── 文件系统
│   ├── ext4/
│   ├── inode/
│   ├── dentry/
│   ├── page-cache/
│   ├── writeback/
│   ├── VFS/
│   ├── bio/
│   ├── iosched/
│   ├── raid/
│   └── LVM/
├── 网络
│   ├── netfilter/
│   ├── iptables/
│   ├── conntrack/
│   ├── route/
│   ├── neigh/
│   ├── socket/
│   ├── tcpip/
│   └── udp/
├── 进程与调度
│   ├── cfs-scheduler/
│   ├── clone/
│   ├── fork/
│   ├── signal/
│   ├── coredump/
│   └── prctl/
├── 内存管理
│   ├── hugepage/
│   ├── ksm/
│   ├── memfd/
│   ├── mmap/
│   ├── vmalloc/
│   └── oom/
├── 安全与隔离
│   ├── capabilities/
│   ├── seccomp/
│   ├── landlock/
│   └── apparmor/
└── 容器技术
    ├── namespace/
    ├── cgroup2/
    ├── container/
    ├── overlayfs/
    └── pivot_root/
```

每个目录包含：
- `*.c` — 可编译运行的演示源码
- `README.md` — 原理讲解与实验步骤
- `Makefile` — 一键编译

---

## 快速上手

```bash
# 克隆项目
git clone https://github.com/golang12306/os-kernel-from-scratch.git
cd os-kernel-from-scratch

# 进入任意 Demo 目录，编译运行
cd demos/futex
make
sudo ./futex_demo

# 查看原理说明
cat demos/futex/README.md
```

> ⚠️ 所有 Demo 均在 Linux x86_64 环境下测试通过。部分实验需要 root 权限。

---

## wandos — 从零写的 OS 内核

如果你想看一个真实的可运行操作系统内核实现：

```bash
git clone https://github.com/golang12306/wandos.git
cd wandos
./build_wsl.sh     # 编译（需要 Linux/WSL）
./run.sh           # 用 QEMU 运行
```

| 模块 | 实现情况 |
|------|---------|
| 引导 | ✅ GRUB2 + multiboot |
| 内存管理 | ✅ 分页 + 伙伴系统 |
| 进程调度 | ✅ 时间片轮转 + 抢占式 |
| 文件系统 | ✅ VFS + MemFS + ConsoleFS |
| 系统调用 | ✅ 完整 ring3→ring0 切换 |
| 设备驱动 | ✅ VGA + 串口 + 键盘 |
| 多核支持 | ✅ SMP + LAPIC + 核间同步 |

---

## 配套公众号文章

每篇公众号文章都配有**可运行的代码 Demo**，学完原理后立刻动手验证。

| 系列 | 篇数 | 状态 |
|------|------|------|
| 内核基础（引导→保护模式→分页） | 10篇 | ✅ 已完结 |
| 基础设施（vDSO/SMP/MESI/kmalloc） | 10篇 | ✅ 已完结 |
| 文件系统（ext4/VFS/writeback） | 10篇 | ✅ 已完结 |
| 网络（socket/TCP/neigh） | 10篇 | 🔄 更新中 |
| 进程与调度 | 10篇 | 📅 待写 |
| 内存管理 | 10篇 | 📅 待写 |
| 安全与隔离 | 10篇 | 📅 待写 |

👉 [查看公众号合集 →](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzAxMjQwMjA5NQ==&action=getalbum&album_id=4239918263758864395)

---

## Star 趋势

如果你觉得这个项目对你有帮助，欢迎 ⭐ Star，你的支持是我持续更新的动力！

---

## 贡献指南

欢迎提交 Demo！要求：

1. 每个 Demo 独立可运行（`make` 能编译）
2. 附 `README.md` 说明原理和实验步骤
3. 提交到对应分类目录下

```bash
# 提 issue 讨论新主题
# 提 PR 添加 Demo 或修复文档
# fork 后自由发挥，欢迎分叉
```

---

<p align="center">
  <strong>学操作系统没有捷径，但有方法。</strong><br>
  从这里开始，从零到精通。
</p>
