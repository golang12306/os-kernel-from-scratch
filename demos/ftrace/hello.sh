#!/bin/bash
# ftrace/hello.sh — ftrace function_graph 演示脚本
# 用法: sudo bash demos/ftrace/hello.sh

set -e

TRACING=/sys/kernel/debug/tracing
FUNC="*read*"

echo "[ftrace 演示脚本]"
echo ""

# 检查权限
if [ "$EUID" -ne 0 ]; then
    echo "需要 root 权限，请用 sudo 运行"
    exit 1
fi

# 检查 debugfs
if [ ! -d "$TRACING" ]; then
    echo "debugfs 未挂载，尝试挂载..."
    mount -t debugfs none /sys/kernel/debug
fi

echo "1. 查看当前 tracer:"
cat $TRACING/current_tracer
echo ""

echo "2. 切换到 function_graph tracer..."
echo function_graph > $TRACING/current_tracer

echo "3. 设置过滤函数: $FUNC"
echo "$FUNC" > $TRACING/set_ftrace_filter

echo "4. 开启追踪..."
echo 1 > $TRACING/tracing_on

echo "5. 执行 read 操作: cat /etc/hostname"
cat /etc/hostname > /dev/null 2>&1 || true

echo "6. 关闭追踪..."
echo 0 > $TRACING/tracing_on

echo ""
echo "7. 追踪结果 (前 50 行):"
echo "================================"
head -50 $TRACING/trace
echo "================================"
echo ""
echo "完整输出: cat $TRACING/trace"

# 恢复
echo nop > $TRACING/current_tracer
echo > $TRACING/set_ftrace_filter
