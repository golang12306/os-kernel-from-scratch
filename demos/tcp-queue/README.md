# TCP 连接队列演示

## 文章核心内容

- listen(backlog)：创建半连接和全连接队列
- 半连接队列（SYN Queue）：三次握手中间状态
- 全连接队列（Accept Queue）：已建立连接等待 accept
- somaxconn：全连接队列实际上限

## 实用命令

```bash
ss -ltn sport = :8080
cat /proc/sys/net/core/somaxconn
cat /proc/sys/net/ipv4/tcp_max_syn_backlog
```
