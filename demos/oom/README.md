# OOM Killer 演示

## 文章核心内容

- oom_score = 内存占用 + 运行时长 + oom_score_adj
- /proc/<pid>/oom_score_adj：调整被杀优先级
- oom_score_adj=-1000：完全免疫
