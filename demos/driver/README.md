# 设备驱动演示

## 文章核心内容

- 字符设备 vs 块设备
- VFS：统一接口
- modprobe/insmod 驱动加载
- Udev 设备节点管理

## 实用命令

```bash
lsmod
modinfo e1000e
udevadm info --query=all --name=sda
```
