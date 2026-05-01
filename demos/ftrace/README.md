# ftrace 演示

## 文章关联

对应公众号文章：《从零写OS内核 | ftrace——Linux 原生的「代码级录像机」》

## 文件说明

| 文件 | 说明 |
|------|------|
| `hello.sh` | 演示 function_graph 追踪 read 系统调用的完整流程 |

## 运行方法

```bash
# 需要 root
sudo bash demos/ftrace/hello.sh
```

## 手动实验步骤

```bash
# 1. 挂载 debugfs（如果未挂载）
sudo mount -t debugfs none /sys/kernel/debug

# 2. 切换到 function_graph
echo function_graph | sudo tee /sys/kernel/debug/tracing/current_tracer

# 3. 过滤函数（可选）
echo "*read*" | sudo tee /sys/kernel/debug/tracing/set_ftrace_filter

# 4. 开启
echo 1 | sudo tee /sys/kernel/debug/tracing/tracing_on

# 5. 运行要追踪的命令
cat /etc/hostname

# 6. 关闭
echo 0 | sudo tee /sys/kernel/debug/tracing/tracing_on

# 7. 查看结果
sudo cat /sys/kernel/debug/tracing/trace
```

## trace-cmd（更友好的命令行工具）

```bash
sudo apt install trace-cmd

# 录制
sudo trace-cmd record -p function_graph -g do_sys_openat2 ls /tmp

# 查看
sudo trace-cmd report
```
