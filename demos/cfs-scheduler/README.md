# CFS 调度器演示

## 文章核心内容

- CFS 用红黑树管理可运行进程
- vruntime = 实际时间 × (1024 / 权重)
- nice 值越低，vruntime 涨得越慢
- sched_latency 控制调度频率
