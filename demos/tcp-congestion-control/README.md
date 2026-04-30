# TCP 拥塞控制 Demo

本目录实现了一个简化版的 TCP 拥塞控制状态机，演示慢启动（Slow Start）和拥塞避免（Congestion Avoidance）的工作原理。

## 文件说明

- `congestion.c` — 拥塞控制状态机完整实现
- `run_demo.sh` — 运行脚本（模拟多轮 ACK + 丢包场景）

## 编译运行

```bash
gcc -o congestion congestion.c && ./congestion
```

## 演示内容

1. **慢启动阶段**：cwnd 从 2 开始，每 ACK 翻倍（指数增长）
2. **进入拥塞避免**：cwnd >= ssthresh 后，线性增长
3. **3-ACK 快速重传**：丢包但对方活着，cwnd 砍半 + 3 MSS
4. **RTO 超时**：最严重丢包，cwnd 重置为 1，重新慢启动

每个状态变化都会打印出 cwnd、ssthresh 和当前状态，方便跟踪窗口变化。
