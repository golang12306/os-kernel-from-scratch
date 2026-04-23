# bio Demo

## 文章对应代码

- `bio.h` / `bio.c` — 简化版 bio 结构和提交接口
- `bio_test.c` — Linux 上观察 bio 生命周期的实验

## 源码文件说明

### bio.h

bio 核心数据结构 + 提交接口：

```c
struct bio_vec {
    struct page *bv_page;   // 内存页
    unsigned int bv_len;    // 长度
    unsigned int bv_offset; // 页内偏移
};

struct bio {
    sector_t bi_sector;       // 磁盘起始扇区
    struct bio_vec *bi_io_vec; // 内存页数组（bio_vec）
    int bi_vcnt;            // bio_vec 数量
    int bi_idx;             // 当前处理到第几个 bio_vec
    unsigned int bi_size;   // 总字节数
    unsigned short bi_op;   // 操作（READ / WRITE）
    struct request *bi_next; // 链到下一个 bio（合并用）
    void *bi_private;      // 私有数据
    bio_end_io_t *bi_end_io; // 完成回调
};
```

### bio.c

bio 生命周期实现：

- `submit_bio()` — 提交 bio 到通用块层
- `make_request_fn()` — 设备驱动处理 bio 的入口
- `bio_add_page()` — 向 bio 添加一个内存页
- `bio_advance()` — bio 完成后推进到下一个 bio_vec

### bio_test.c

Linux 上的观察实验：
- 用 perf 监控 bio 提交事件
- 用 iostat 观察磁盘 IO

---

## 文章核心内容

- bio 是块设备 IO 的基本单位
- bio_vec 数组：描述 bio 涉及的所有内存页
- submit_bio() 流程：VFS → page cache → bio → request → 块设备驱动
- bio 合并：相邻扇区的 IO 自动合并成一个大 bio
- bio 分裂：大 bio 超过设备最大 IO 大小时自动分裂
