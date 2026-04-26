# neigh / ARP 子系统演示

## 文章核心内容

- ARP 协议：IP → MAC 的桥梁
- Linux neigh 子系统的状态机（NUD 状态）
- 邻居发现流程：广播请求 → 单播响应 → 缓存 MAC
- GARP（Gratuitous ARP）的原理与用途

## Demo 说明

### neigh_arp.c

纯用户态 ARP 解析程序，演示：

- 构造 ARP Request 并发送（原始套接字）
- 接收并解析 ARP Response
- 从响应中提取目标主机的 MAC 地址

**编译**：
```bash
gcc -o neigh_arp neigh_arp.c
```

**运行**（需要 root）：
```bash
sudo ./neigh_arp <目标IP> <本机IP> <本机MAC> <网卡名>
# 示例：
sudo ./neigh_arp 192.168.1.1 192.168.1.100 00:11:22:33:44:55 eth0
```

**观察方法**（另一个终端）：
```bash
# 抓 ARP 包
sudo tcpdump -i eth0 -n arp
```

## 相关命令

```bash
# 查看 ARP 缓存
ip neigh show

# 查看所有状态的条目（包括 STALE/FAILED）
ip neigh show nud all

# 手动添加静态 ARP
ip neigh add 192.168.1.1 lladdr dc:fe:72:4a:1b:00 dev eth0 nud permanent

# 删除 ARP 条目
ip neigh del 192.168.1.1 dev eth0

# 清空 ARP 缓存
ip neigh flush all

# 手动发 GARP
arping -A -I eth0 192.168.1.100
```

## 相关文章

- 第 234 篇：路由表 — 数据包是怎么被送出去的
- 第 236 篇：socket — 一切网络通信的起点
