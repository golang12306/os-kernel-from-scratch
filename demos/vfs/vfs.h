// vfs.h — 简化版 VFS 接口（自研 OS 内核用）
#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "list.h"
#include "spinlock.h"

// ============================================================
// 核心数据结构
// ============================================================

// 文件操作（每个文件对象持有）
struct file_operations {
    int (*open)(struct inode *, struct file *);
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
    int (*release)(struct inode *, struct file *);
    int (*fsync)(struct inode *, int datasync);
};

// inode 操作（每个 inode 对象持有）
struct inode_operations {
    int (*create)(struct inode *, const char *, mode_t, struct dentry **);
    struct dentry *(*lookup)(struct inode *, const char *, struct dentry **);
    int (*mkdir)(struct inode *, const char *, mode_t);
    int (*rmdir)(struct inode *, const char *);
    int (*rename)(struct inode *, const char *, struct inode *, const char *);
};

// 目录项（dentry = 目录条目的内存表示）
struct dentry {
    char name[256];                 // 目录项名称
    struct inode *inode;           // 指向对应的 inode
    struct dentry *parent;         // 父目录
    struct list_head children;     // 子目录链表
    struct list_head hash;         // 哈希链表（dcache 用）
    int refcount;                  // 引用计数
};

// inode（文件/目录在内存中的表示）
struct inode {
    unsigned long ino;             // inode 编号
    mode_t i_mode;                // 文件类型 + 权限
    unsigned long i_size;         // 文件大小
    void *i_private;              // 文件系统私有数据
    struct inode_operations *i_op; // inode 操作
    struct file_operations *i_fop; // 文件操作
    struct super_block *i_sb;     // 所属超级块
    struct list_head i_dentry;     // 关联的 dentry 链表
    atomic_t i_count;             // 引用计数
};

// file（进程打开文件的表示）
struct file {
    struct dentry *f_dentry;       // 关联的 dentry
    struct file_operations *f_op; // 文件操作
    loff_t f_pos;                 // 当前文件偏移
    unsigned int f_flags;         // O_RDONLY, O_WRONLY, O_RDWR 等
    void *private_data;           // 文件系统私有数据
};

// super_block（一个已挂载文件系统的元数据）
struct super_block {
    char mnt_point[256];          // 挂载点路径
    struct filesystem_type *s_type; // 文件系统类型
    void *s_fs_info;              // 文件系统私有数据
    struct inode *s_root_inode;    // 根目录 inode
    struct list_head s_inodes;     // 该文件系统所有 inode
    struct list_head s_dirty;      // dirty inode 链表
};

// filesystem_type（注册的文件系统类型）
struct filesystem_type {
    const char *name;              // 文件系统名称，如 "ext4", "xfs"
    int (*mount)(struct super_block *, const char *, void *);
    struct list_head list;         // 注册链表
    struct list_head next;         // 同类型链表
};

// ============================================================
// 全局变量
// ============================================================

// 所有注册的文件系统
extern struct list_head registered_filesystems;

// VFS 根目录（/）
extern struct dentry *vfs_root_dentry;

// ============================================================
// 核心 API
// ============================================================

// 注册一个文件系统类型
int register_filesystem(struct filesystem_type *fs);

// 挂载文件系统
int vfs_mount(struct filesystem_type *fs_type, struct dentry *target, 
              const char *mnt_point, void *data);

// 打开文件
struct file *vfs_open(const char *pathname, int flags, mode_t mode);

// 读取文件
ssize_t vfs_read(struct file *filp, char *buf, size_t count, loff_t *pos);

// 写入文件
ssize_t vfs_write(struct file *filp, const char *buf, size_t count, loff_t *pos);

// 关闭文件
int vfs_close(struct file *filp);

// 创建 inode
struct inode *vfs_new_inode(struct super_block *sb, mode_t mode);

// 创建 dentry
struct dentry *vfs_new_dentry(const char *name, struct inode *inode);

// 分配 inode 编号
unsigned long vfs_alloc_ino(struct super_block *sb);

// VFS 初始化
void vfs_init(void);

#endif // VFS_H
