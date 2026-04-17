# 虚拟化演示

## 文章核心内容

- VT-x / AMD-V：CPU 硬件虚拟化扩展
- KVM：Linux 内核 Hypervisor 驱动
- EPT：内存虚拟化（两层页表自动翻译）
- virtio：半虚拟化 I/O，共享内存替代频繁 VM Exit

## 实用命令

```bash
# 检查 CPU 硬件虚拟化支持
grep -E '(vmx|svm)' /proc/cpuinfo | head -1

# 检查 KVM 是否可用
ls /dev/kvm

# 启动一个 KVM 加速的 VM
qemu-system-x86_64 -m 512M -hda /dev/zero -enable-kvm -nographic
```
