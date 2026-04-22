# Writeback Demo

## 文章对应代码

- `wb_test.c` — Linux 上观察 writeback 的实验程序
- `wb_sim.h` / `wb_sim.c` — 简化版 writeback 实现（自研 OS 用）

## 源码文件说明

### wb_test.c

Linux 上的实验程序，演示：

- dirty page 的产生和观测（`/proc/meminfo`）
- `sync` / `fsync` 的作用区别
- writeback 延迟测量

编译运行：

```bash
gcc -O2 wb_test.c -o wb_test
./wb_test
```

### wb_sim.h / wb_sim.c

自研 OS 内核的简化 writeback 实现：

- `struct dirty_page` — 脏页描述符
- `struct inode_wb_state` — inode 级别的回写状态
- `flush_thread()` — 后台 flush 线程
- `writeback_inode()` — 单个 inode 的回写

---

## 文章核心内容

- writeback 机制：dirty page 的异步回写
- dirty_ratio / dirty_background_ratio 阈值
- sync / fsync / fdatasync 的区别
- writeback 延迟测量
