# 并发与同步演示

## 目录

- `race.c` — 竞争条件演示（用 mutex 修复）

## 编译运行

```bash
gcc race.c -lpthread -o race && ./race
# counter = 2000000（每次都正确）
```

## 关键点

- 原子操作（atomic）适用于简单读-改-写
- mutex 让进程睡眠，不浪费 CPU
- 自旋锁适用于锁持有时间极短的场景
- 32核服务器上，锁分散到每个核能大幅提升性能
