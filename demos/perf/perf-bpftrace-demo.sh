#!/bin/bash
# perf/perf-bpftrace-demo.sh — perf + bpftrace 联动演示
# 需要：linux-perf, bpftrace, FlameGraph

set -e

echo "[perf + bpftrace 联动演示]"
echo ""

if [ "$EUID" -ne 0 ]; then
    echo "需要 root 权限"
    exit 1
fi

# 检查工具
command -v bpftrace >/dev/null 2>&1 || { echo "bpftrace 未安装"; exit 1; }

echo "=========================================="
echo "【演示一】bpftrace 统计系统调用频率（10 秒）"
echo "=========================================="
echo "（按 Ctrl-C 停止，或等待 10 秒自动结束）"
echo ""

# 用 timeout 限制 10 秒
timeout 10 bpftrace -e '
    tracepoint:syscalls:sys_enter_openat
    {
        @opens[comm] = count();
    }
    interval:s:3
    {
        print(@opens);
        clear(@opens);
    }
' 2>&1 || true

echo ""
echo "=========================================="
echo "【演示二】bpftrace 统计块设备 IO 延迟"
echo "=========================================="
echo "（需要实际 IO 才能看到数据，演示仅展示脚本结构）"
echo ""

timeout 10 bpftrace -e '
    kprobe:blk_mq_start_request
    {
        @start[args->rq->tag] = nsecs;
    }
    kprobe:blk_account_io_done
    /@start[args->rq->tag]/
    {
        $lat = (nsecs - @start[args->rq->tag]) / 1000;
        @us_lat[$lat > 1000 ? "slow" : "fast"] = count();
        delete(@start[args->rq->tag]);
    }
    interval:s:3
    {
        print(@us_lat);
        clear(@us_lat);
    }
' 2>&1 || true

echo ""
echo "=========================================="
echo "【演示三】perf record + perf script 工作流"
echo "=========================================="
echo "（短时采样，演示命令结构）"
echo ""

echo "perf record -F 99 -a -g -- sleep 5"
timeout 6 perf record -F 99 -a -g -- sleep 5 2>&1 || true

echo ""
echo "perf script（显示采样数据，前 10 行）："
perf script 2>&1 | head -20

echo ""
echo "完整演示完成！"
echo ""
echo "工作流总结："
echo "  1. perf record 高频采样    → perf.data"
echo "  2. perf script 导出        → 可供 bpftrace 分析"
echo "  3. bpftrace 处理 perf 输出 → 条件过滤 + 聚合"
echo "  4. FlameGraph 生成火焰图   → 可视化"
