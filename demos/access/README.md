# access 演示

## 文章核心内容

- access(path, R_OK/W_OK/X_OK/F_OK)
- 用真实 UID 检查（不是 effective UID）
- 安全检查在 open 之前
