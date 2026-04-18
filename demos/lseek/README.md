# lseek 演示

## 文章核心内容

- lseek(fd, offset, SEEK_SET/CUR/END)
- 可以创建空洞文件
- ftell 等价于 lseek(fd, 0, SEEK_CUR)
