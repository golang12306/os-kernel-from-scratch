# copy_to_user 演示

## 文章核心内容

- 内核态访问用户态内存必须通过 copy_from_user()
- access_ok() 做边界检查
- SMAP/SMEP 硬件保护
- 用户态地址和内核态地址通过页表隔离
