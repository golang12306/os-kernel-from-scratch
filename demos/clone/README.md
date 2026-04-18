# clone 演示

## 文章核心内容

- clone = Linux 创建进程/线程的统一接口
- fork = clone(CLONE_VM=0)
- pthread_create = clone(CLONE_VM|CLONE_FS|CLONE_FILES...)
