# perf + bpftrace 联动演示

## 文章关联

对应公众号文章：《从零写OS内核 | perf + bpftrace 联动——线上故障抢救实战》

## 文件说明

| 文件 | 说明 |
|------|------|
| `perf-demo.sh` | perf + FlameGraph 基础演示 |
| `perf-bpftrace-demo.sh` | perf + bpftrace 联动演示 |

## perf + bpftrace 联动工作流

```
perf record 高频采样  →  perf.data
        ↓
perf script 导出     →  perf.trace（文本流）
        ↓
bpftrace 二次分析    →  条件过滤 + 聚合
        ↓
FlameGraph 可视化    →  flame.svg
```

## 运行演示

```bash
# 需要 root
sudo bash demos/perf/perf-bpftrace-demo.sh
```

## 常用联动命令速查

```bash
# 1. perf 高频采样
perf record -F 999 -a -g -- sleep 60

# 2. 导出给 bpftrace 分析
perf script -i perf.data | bpftrace -e '
    /pid == 12345/ {
        @[ustack(10)] = count();
    }
'

# 3. 生成火焰图
perf script -i perf.data | ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl > flame.svg

# 4. off-CPU 火焰图
perf script -i perf.data | ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl --color=io > offcpu-flame.svg
```
