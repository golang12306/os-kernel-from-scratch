# 信号与进程退出演示

## 文章核心内容

- 信号是异步事件机制（整数编号）
- SIGTERM 可捕获，SIGKILL 无法捕获
- 进程退出：do_exit → exit_notify → 发送 SIGCHLD 给父进程
- D 状态进程 kill -9 杀不死（等待 I/O）
- Zombie：进程已 exit 但父进程未回收

## 实用命令

```bash
# 查看进程信号状态
cat /proc/<pid>/status | grep Sig

# 查看 D 状态进程
ps aux | grep -E 'D\s'

# 追踪 kill 系统调用
strace -f -p <pid> -e signal=none
```
