# truncate / ftruncate 演示

## 文章核心内容

- truncate(path, len) 改变文件大小
- len > 原大小 = 扩展（填 0）
- len < 原大小 = 截断
