# CPU 缓存演示

## 文章核心内容

- L1/L2/L3 缓存层级和延迟
- Cache Line（64B）
- 空间局部性：顺序访问 vs 跳跃访问
- False Sharing

## 实用命令

```bash
perf stat -e cache-misses,cache-references ./cache_test
```
