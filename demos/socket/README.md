# socket 演示

对应文章：**从零写OS内核 | socket：每个网络连接出生的地方，但它的真身你见过吗？（ID: 236）**

## 核心内容

- `socket(AF_INET, SOCK_STREAM, 0)` 创建 TCP socket，返回 fd
- `bind()` 把 socket 绑定到 `0.0.0.0:9999`
- `listen()` 开始监听，backlog = 5
- `accept()` 阻塞等待客户端连接
- `read/write` 收发数据
- `close()` 关闭连接

## 编译运行

```bash
gcc -o mini-socket mini-socket.c
./mini-socket &
sleep 1
echo -e "GET / HTTP/1.0\r\n\r\n" | nc localhost 9999
```

## 观察 socket 状态

```bash
ss -tn | grep 9999
# LISTEN  ← 服务端
# ESTAB   ← nc 连接后
```
