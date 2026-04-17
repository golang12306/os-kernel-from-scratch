# 时间管理演示

## 文章核心内容

- RTC：CMOS 实时时钟，关机独立运行
- jiffies：内核节拍计数器，精度 1/HZ 秒
- TSC：CPU 时间戳计数器，纳秒级精度
- hrtimer：纳秒级高精度定时器
- NTP：时钟频率校准

## 实用命令

```bash
# 查看 NTP 同步状态
timedatectl

# 查看 HZ 值
getconf HZ

# 观察时间精度
./time_precision
```
