# 容器技术演示

## Namespace + Cgroup 手动隔离

```bash
# 创建 PID Namespace（新进程看到独立的 PID 空间）
unshare --pid --fork --mount-proc /bin/bash
# 在这个 bash 里，ps 看到的是从 PID 1 开始

# 限制内存（cgroup）
mkdir -p /sys/fs/cgroup/memory/mytest
echo 256m > /sys/fs/cgroup/memory/mytest/memory.limit_in_bytes
echo $$ > /sys/fs/cgroup/memory/mytest/tasks
# 当前 shell 最多只能用 256MB 内存
```

## 关键概念

- **Namespace**：隔离视图（PID、网络、挂载点）
- **Cgroup**：隔离资源（CPU、内存配额）
- **OverlayFS**：联合挂载文件系统（镜像层 + 容器层）
