# wait/waitpid 演示

## 文章核心内容

- wait(NULL) = 等任意子进程
- waitpid(pid, &status, WNOHANG)
- SIGCHLD + 僵尸进程
