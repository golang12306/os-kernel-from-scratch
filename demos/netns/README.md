# 容器网络演示

## 文章核心内容

- netns：网络命名空间隔离
- veth pair：连接两个 netns 的虚拟网线
- bridge：连接多个 veth 形成交换网络
- iptables：NAT 和端口映射

## 实用命令

```bash
ip netns add test
ip link add veth1 type veth peer name veth2
ip link set veth1 netns test
ip netns exec test ip link
```
