# futex 演示

futex（Fast Userspace Mutex）是 Linux 高性能锁的底层基础。
几乎所有现代互斥锁（pthread_mutex、std::mutex）的实现都依赖 futex。

## 文章核心内容

- **futex 的核心思想**：把锁竞争最常见路径（无竞争 / 锁快速释放）留在用户态，只有必要时才进内核
- **futex_wait()**：用户态原子操作失败后，进内核让线程真正睡下
- **futex_wake()**：锁释放时通知内核唤醒等待线程
- **先改值再唤醒**：atomic_store(&lock, 0) 在 futex_wake() 之前，顺序绝对不能反
- **spurious wakeup**：futex_wait 可能被信号打断，被唤醒后必须重新检查锁值
- **丢失唤醒的防护**：FUTEX_WAIT 校验机制——进内核时再次检查 *uaddr == val，不相等则不睡

## 文件说明

| 文件 | 说明 |
|------|------|
| `futex_v1_spinlock.c` | 纯用户态自旋锁，atomic CAS，无系统调用 |
| `futex_v2_yield_spinlock.c` | 自旋 1000 次后 sched_yield()，减少 CPU 浪费 |
| `futex_v3_futex_lock.c` | 基础 futex 锁：用户态 CAS 失败后进内核睡下 |
| `futex_v4_final.c` | 完整版：处理 spurious wakeup，4 线程测试 |

## 编译

```bash
gcc -o futex_v1 futex_v1_spinlock.c -pthread
gcc -o futex_v2 futex_v2_yield_spinlock.c -pthread
gcc -o futex_v3 futex_v3_futex_lock.c -pthread
gcc -o futex_v4 futex_v4_final.c -pthread
```

## 运行 & 验证

```bash
# 运行
./futex_v3
# 输出：counter = 200000（两个线程各加 100000 次）

# strace 验证系统调用
strace -f -e futex ./futex_v3 2>&1 | grep -E "futex|FUTEX"

# perf 统计 futex 调用次数
perf stat -e 'syscalls:sys_enter_futex' ./futex_v4
```

## 验证要点

1. **v1/v2（自旋锁）**：无 futex 系统调用，counter 正确，但 CPU 空转
2. **v3/v4（futex 锁）**：低竞争时 futex 调用少，高竞争时才大量触发
3. **strace**：观察 FUTEX_WAIT 和 FUTEX_WAVE 的返回值
4. **futex_wake 第二个参数**：1 = 唤醒 1 个线程

## 踩坑提示

- futex_unlock 必须先 `atomic_store(&lock, 0)` 再 `futex_wake`，顺序不能反
- futex 只能用于同一进程内的线程同步，跨进程需要共享内存 futex
- spurious wakeup 必须循环重试，不能假设醒来就一定拿到锁
