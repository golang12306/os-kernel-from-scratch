# 僵尸进程演示

## 文章核心内容

- exit() 保留 task_struct
- wait()/waitpid() 回收
- PF_EXITING + SIGCHLD
- 孤儿被 init 收养
