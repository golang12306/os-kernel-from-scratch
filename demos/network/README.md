# 网络协议栈演示

## 文章核心内容

- Ring Buffer：网卡 DMA 缓冲区
- 中断处理：网卡驱动收到包后放入协议栈
- IP 层：查路由表，决定本地处理还是转发
- TCP 层：四元组哈希匹配找到 socket
- Socket 队列：内核到用户态的数据通道

## 实用命令

```bash
# 抓包观察网络包
tcpdump -i any -n 'tcp and port 8080'

# 查看 TCP 统计
cat /proc/net/snmp | grep Tcp

# 查看 socket 状态
ss -tlnp
```
