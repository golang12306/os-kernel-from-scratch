# TCP/IP 演示

## 文章核心内容

- TCP/IP 四层模型：链路层 → 网络层 → 传输层 → 应用层
- TCP vs UDP 的区别
- socket API 是传输层接口，封装了内核的网络协议栈

## 对应文章

**237 · TCP三次握手**：[从零写OS内核：TCP三次握手——连接建立前，发生了什么？](https://mp.weixin.qq.com/s?__biz=MzAxMjQwMjA5NQ==&mid=...&idx=1&sn=...)

## TCP 三次握手演示

### 编译

```bash
gcc -o tcp_handshake tcp_handshake.c
```

### 运行

```bash
# 终端1（服务端）
./tcp_handshake server

# 终端2（客户端）
./tcp_handshake client
```

### 观察连接状态

```bash
# 在另一个终端，持续观察
watch -n 0.1 'ss -tn | grep 9998'
```

### 程序输出示例

**服务端：**
```
=== TCP 三次握手 - 服务端 ===
[0ms] 创建 socket
[0ms] bind(port=9998)
[0ms] listen(backlog=5)
[0ms] 服务端已启动，等待客户端连接...
[124ms] accept() 返回！客户端连接建立
  客户端: 127.0.0.1:xxxxx
  [状态] TCP_STATE = ESTABLISHED
[125ms] 收到客户端消息: Hello from client!
[125ms] 关闭连接
```

**客户端：**
```
=== TCP 三次握手 - 客户端 ===
[0ms] 创建 socket
[0ms] connect(127.0.0.1:9998) -- 触发三次握手
  ↓ SYN 被发送
[123ms] connect() 返回！连接建立完成
  [状态] TCP_STATE = ESTABLISHED
[123ms] 发送数据: "Hello from client!"
[124ms] 收到服务端消息: Hello from server!
[124ms] 关闭连接
```

## TCP 状态机

三次握手路径：

```
客户端：                              服务端：
  │                                    │
CLOSED                              LISTEN
  │                                    │
  │  ① SYN(seq=x)                     │
  │  ───────────────────────────────>│ SYN_RECEIVED
  │                                    │
  │           ② SYN+ACK(seq=y,ack=x+1)│
  │  <─────────────────────────────── │
  │                                    │
  │  ③ ACK(ack=y+1)                   │
  │  ────────────────────────────────>│ ESTABLISHED
  │                                    │
ESTABLISHED                          ESTABLISHED
  │                                    │
  │         连接建立！                   │
```

| 状态 | 说明 |
|------|------|
| CLOSED | 无连接 |
| LISTEN | 服务端监听中（listen() 后） |
| SYN_SENT | 客户端已发送 SYN |
| SYN_RECEIVED | 服务端收到 SYN，已发 SYN+ACK |
| ESTABLISHED | 连接建立完成 |

## 相关阅读

- `man tcp` — TCP 协议参数
- `man connect` — connect 系统调用
- `ss -tn` — 查看 TCP 连接状态
- Linux 内核源码：`net/ipv4/tcp_input.c`
