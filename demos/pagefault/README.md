# Page Fault 演示

## 文章核心内容

- Minor Fault：页在内存但未建立页表映射
- Major Fault：页需要从 swap/磁盘读入
- Invalid Fault：地址不在 VMA → SIGSEGV
- do_page_fault 处理流程

## 实用命令

```bash
cat /proc/<pid>/status | grep PageFault
perf stat -e page-faults ./program
```
