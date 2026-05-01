#!/bin/bash
# perf/perf-demo.sh — perf + FlameGraph 演示脚本
# 需要：linux-perf, FlameGraph (https://github.com/brendangregg/FlameGraph)

set -e

echo "[perf + FlameGraph 演示]"
echo ""

# 检查权限
if [ "$EUID" -ne 0 ]; then
    echo "需要 root 权限"
    exit 1
fi

# 检查 perf
if ! command -v perf &> /dev/null; then
    echo "perf 未安装，尝试安装..."
    sudo apt install linux-perf
fi

# 检查 FlameGraph
if [ ! -d "./FlameGraph" ]; then
    echo "FlameGraph 未安装，克隆..."
    git clone https://github.com/brendangregg/FlameGraph.git
fi

echo "1. perf stat — 全局性能统计（5 秒）"
echo "=========================================="
perf stat -a sleep 5 2>&1
echo ""

echo "2. perf list — 查看可用采样事件"
echo "=========================================="
perf list 2>&1 | grep -E "Hardware|Software|Tracepoint" | head -20
echo ""

echo "3. perf record — 采样 5 秒（请运行一个程序...）"
echo "=========================================="
echo "运行 perf record -F 99 -a -g -- sleep 5"
perf record -F 99 -a -g -- sleep 5 2>&1
echo ""

echo "4. perf report — 查看采样结果"
echo "=========================================="
perf report --stdio -g none 2>&1 | head -30
echo ""

echo "5. 生成火焰图（SVG）"
echo "=========================================="
perf script | ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl > flame.svg 2>&1
echo "火焰图已生成: flame.svg"
echo "用浏览器打开查看"

echo ""
echo "演示完成！"
echo "flame.svg 可以下载到本地用浏览器打开（支持缩放）"
