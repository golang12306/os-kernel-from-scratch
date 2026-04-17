# proc 文件系统演示

## 文章核心内容

- procfs 是内核动态生成的伪文件系统
- /proc/meminfo：内存信息
- /proc/<pid>/status：进程状态
- /proc/sys/：可调的内核参数
- /proc/self/：自动指向当前进程

## 常用命令

```bash
cat /proc/meminfo
cat /proc/cpuinfo
cat /proc/loadavg
cat /proc/sys/kernel/pid_max
```
