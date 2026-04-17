# 内核调试演示

## 文章核心内容

- dmesg：内核环形缓冲区读取
- OOM Killer：内存耗尽时的进程选择策略
- Kernel Panic：内核崩溃的堆栈回溯
- journalctl：systemd 日志查询

## 常用命令

```bash
# 内核日志
dmesg --last 200
dmesg --level=err
dmesg -w  # 实时监控

# OOM Killer 详情
dmesg | grep -A5 oom

# systemd 日志
journalctl -p err --since "1 hour ago"
journalctl -b -u nginx

# 一键采集诊断信息
./diagnose.sh
```
