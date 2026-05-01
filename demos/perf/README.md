# perf + FlameGraph 演示

## 文章关联

对应公众号文章：《从零写OS内核 | perf + FlameGraph——火焰图：让 CPU 热点一目了然》

## 文件说明

| 文件 | 说明 |
|------|------|
| `perf-demo.sh` | 完整演示：perf stat / record / report + 生成火焰图 |

## 运行环境准备

```bash
# 安装 perf（Ubuntu/Debian）
sudo apt install linux-perf

# 安装 FlameGraph
git clone https://github.com/brendangregg/FlameGraph.git

# 检查 perf 权限
cat /proc/sys/kernel/perf_event_paranoid
# 如果是 3，改成 2（允许普通用户采样）
echo 2 | sudo tee /proc/sys/kernel/perf_event_paranoid
```

## 运行演示脚本

```bash
# 需要 root
sudo bash demos/perf/perf-demo.sh
```

## 手动完整工作流

```bash
# 1. 采样（对整个系统采样 60 秒）
sudo perf record -F 99 -a -g -- sleep 60

# 2. 生成火焰图
sudo perf script | ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl > flame.svg

# 3. 下载 flame.svg 到本地，用浏览器打开
```

## 对特定进程采样

```bash
# 找到进程 PID
ps aux | grep my_program

# 对该进程采样 30 秒
sudo perf record -F 999 -p <PID> -g -- sleep 30

# 查看报告
sudo perf report
```

## perf 常用命令速查

```bash
perf stat -p <PID>     # 全局统计
perf top                # 实时 top（类似 top，但基于采样）
perf record -a -g       # 全系统采样 + 调用栈
perf report             # 查看报告
perf annotate           # 查看某函数的指令级详情
perf diff               # 对比两次采样差异
```
