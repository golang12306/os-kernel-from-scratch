# umask 演示

## 文章核心内容

- umask(mask) 设置文件创建掩码
- mode & ~umask = 最终权限
- umask(022) 禁止组写/其他写
