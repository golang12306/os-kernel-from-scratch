# 内存回收演示

## 文章核心内容

- watermark 水位：high/low/min 三级水位
- kswapd：后台内存回收线程
- LRU：two-list active/inactive
- anonymous 页面 vs file-backed 页面

## 实用命令

```bash
# 查看内存水位
cat /proc/zoneinfo | grep -A5 "Node.*low"

# 查看 swap 使用
swapon -s

# 查看页面回收统计
cat /proc/vmstat | grep pgsteal
```
