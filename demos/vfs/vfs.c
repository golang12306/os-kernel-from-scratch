// vfs.c — 简化版 VFS 实现（自研 OS 内核用）
#include "vfs.h"
#include "memory.h"
#include "sched.h"
#include "string.h"
#include "printf.h"

// ============================================================
// 全局变量
// ============================================================

// 所有注册的文件系统
struct list_head registered_filesystems = LIST_INIT(registered_filesystems);

// VFS 根目录
struct dentry *vfs_root_dentry = NULL;

// 下一个可用的 inode 编号
static unsigned long next_ino = 1;

// inode 表（简化：全局数组）
#define MAX_INODES 1024
static struct inode inode_table[MAX_INODES];
static unsigned long inode_inuse[MAX_INODES / 64];
static spinlock_t inode_table_lock;

// dentry 哈希表（简化：数组）
#define DENTRY_HASH_SIZE 256
static struct list_head dentry_hashtable[DENTRY_HASH_SIZE];
static spinlock_t dentry_hash_lock;

// ============================================================
// inode 管理
// ============================================================

static unsigned int hash_dentry(const char *name) {
    unsigned int hash = 5381;
    int c;
    while ((c = *name++)) {
        hash = ((hash << 5) + hash) + c;  // djb2 hash
    }
    return hash % DENTRY_HASH_SIZE;
}

void vfs_init(void) {
    printf("[vfs] initializing VFS...\n");
    
    // 初始化 inode 表
    memset(inode_table, 0, sizeof(inode_table));
    memset(inode_inuse, 0, sizeof(inode_inuse));
    spinlock_init(&inode_table_lock);
    
    // 初始化 dentry 哈希表
    for (int i = 0; i < DENTRY_HASH_SIZE; i++) {
        init_list_head(&dentry_hashtable[i]);
    }
    spinlock_lock(&dentry_hash_lock);
    spinlock_init(&dentry_hash_lock);
    
    printf("[vfs] VFS initialized\n");
}

unsigned long vfs_alloc_ino(struct super_block *sb) {
    unsigned long ino;
    
    spinlock_lock(&inode_table_lock);
    ino = next_ino++;
    spinlock_unlock(&inode_table_lock);
    
    return ino;
}

struct inode *vfs_new_inode(struct super_block *sb, mode_t mode) {
    struct inode *inode = NULL;
    
    spinlock_lock(&inode_table_lock);
    
    for (int i = 0; i < MAX_INODES; i++) {
        int word = i / 64;
        int bit = i % 64;
        if (!(inode_inuse[word] & (1UL << bit))) {
            inode_inuse[word] |= (1UL << bit);
            inode = &inode_table[i];
            break;
        }
    }
    
    spinlock_unlock(&inode_table_lock);
    
    if (!inode) {
        printf("[vfs] ERROR: no free inodes\n");
        return NULL;
    }
    
    // 初始化 inode
    memset(inode, 0, sizeof(struct inode));
    inode->ino = vfs_alloc_ino(sb);
    inode->i_mode = mode;
    inode->i_size = 0;
    inode->i_sb = sb;
    inode->i_private = NULL;
    atomic_set(&inode->i_count, 1);
    init_list_head(&inode->i_dentry);
    
    // 加入 super_block 的 inode 链表
    spinlock_lock(&sb->s_inodes_lock);
    list_add_tail(&inode->i_sb_node, &sb->s_inodes);
    spinlock_unlock(&sb->s_inodes_lock);
    
    printf("[vfs] new inode: ino=%lu mode=%o\n", inode->ino, mode);
    
    return inode;
}

struct dentry *vfs_new_dentry(const char *name, struct inode *inode) {
    struct dentry *dentry;
    
    dentry = kmalloc(sizeof(struct dentry), GFP_KERNEL);
    if (!dentry) return NULL;
    
    strncpy(dentry->name, name, sizeof(dentry->name) - 1);
    dentry->name[sizeof(dentry->name) - 1] = '\0';
    dentry->inode = inode;
    dentry->parent = NULL;
    init_list_head(&dentry->children);
    init_list_head(&dentry->hash);
    dentry->refcount = 1;
    
    // 增加 inode 引用计数
    atomic_inc(&inode->i_count);
    
    // 加入 inode 的 dentry 链表
    list_add_tail(&dentry->dentry_node, &inode->i_dentry);
    
    // 加入 dentry 哈希表
    unsigned int h = hash_dentry(name);
    list_add_tail(&dentry->hash, &dentry_hashtable[h]);
    
    return dentry;
}

void vfs_put_dentry(struct dentry *dentry) {
    if (!dentry) return;
    
    dentry->refcount--;
    if (dentry->refcount > 0) return;
    
    // 从哈希表移除
    list_del(&dentry->hash);
    
    // 从父目录的 children 链表移除
    if (dentry->parent) {
        list_del(&dentry->child_node);
    }
    
    // 释放 inode
    if (dentry->inode) {
        atomic_dec(&dentry->inode->i_count);
        vfs_put_inode(dentry->inode);
    }
    
    kfree(dentry);
}

void vfs_put_inode(struct inode *inode) {
    if (!inode) return;
    
    if (atomic_dec_and_test(&inode->i_count)) {
        // 调用文件系统的 evict inode 回调
        if (inode->i_op && inode->i_op->evict_inode) {
            inode->i_op->evict_inode(inode);
        }
        
        // 从 super_block 的 inode 链表移除
        spinlock_lock(&inode->i_sb->s_inodes_lock);
        list_del(&inode->i_sb_node);
        spinlock_unlock(&inode->i_sb->s_inodes_lock);
        
        // 释放 inode 表项
        int idx = inode - inode_table;
        int word = idx / 64;
        int bit = idx % 64;
        spinlock_lock(&inode_table_lock);
        inode_inuse[word] &= ~(1UL << bit);
        spinlock_unlock(&inode_table_lock);
    }
}

// ============================================================
// 文件系统注册和挂载
// ============================================================

int register_filesystem(struct filesystem_type *fs) {
    // 检查是否已注册
    struct list_head *pos;
    list_for_each(pos, &registered_filesystems) {
        struct filesystem_type *e = list_entry(pos, struct filesystem_type, list);
        if (strcmp(e->name, fs->name) == 0) {
            printf("[vfs] filesystem '%s' already registered\n", fs->name);
            return -EEXIST;
        }
    }
    
    list_add_tail(&fs->list, &registered_filesystems);
    printf("[vfs] registered filesystem: '%s'\n", fs->name);
    return 0;
}

int vfs_mount(struct filesystem_type *fs_type, struct dentry *target,
              const char *mnt_point, void *data) {
    struct super_block *sb;
    
    sb = kmalloc(sizeof(struct super_block), GFP_KERNEL);
    if (!sb) return -ENOMEM;
    
    memset(sb, 0, sizeof(struct super_block));
    strncpy(sb->mnt_point, mnt_point, sizeof(sb->mnt_point) - 1);
    sb->s_type = fs_type;
    sb->s_fs_info = NULL;
    sb->s_root_inode = NULL;
    init_list_head(&sb->s_inodes);
    init_list_head(&sb->s_dirty);
    spinlock_init(&sb->s_inodes_lock);
    spinlock_init(&sb->s_dirty_lock);
    
    // 调用文件系统的 mount 回调
    if (fs_type->mount) {
        int ret = fs_type->mount(sb, mnt_point, data);
        if (ret < 0) {
            printf("[vfs] mount failed for '%s'\n", fs_type->name);
            kfree(sb);
            return ret;
        }
    }
    
    printf("[vfs] mounted '%s' at '%s'\n", fs_type->name, mnt_point);
    return 0;
}

// ============================================================
// 文件操作
// ============================================================

struct file *vfs_open(const char *pathname, int flags, mode_t mode) {
    struct dentry *dentry;
    struct inode *inode;
    struct file *filp;
    
    printf("[vfs] open('%s', 0x%x, 0%o)\n", pathname, flags, mode);
    
    // 查找路径对应的 dentry（简化：只用根目录）
    if (strcmp(pathname, "/") == 0) {
        dentry = vfs_root_dentry;
    } else {
        // 简化：从根目录查找
        const char *name = pathname + 1;  // 跳过前导 '/'
        if (vfs_root_dentry && vfs_root_dentry->inode) {
            struct inode_operations *iop = vfs_root_dentry->inode->i_op;
            if (iop && iop->lookup) {
                dentry = iop->lookup(vfs_root_dentry->inode, name, NULL);
            } else {
                dentry = NULL;
            }
        } else {
            dentry = NULL;
        }
    }
    
    if (!dentry || !dentry->inode) {
        printf("[vfs] open: path not found '%s'\n", pathname);
        return NULL;
    }
    
    inode = dentry->inode;
    
    // 检查是否是目录
    if (S_ISDIR(inode->i_mode)) {
        printf("[vfs] open: is a directory '%s'\n", pathname);
        return NULL;
    }
    
    // 分配 file 对象
    filp = kmalloc(sizeof(struct file), GFP_KERNEL);
    if (!filp) return NULL;
    
    memset(filp, 0, sizeof(struct file));
    filp->f_dentry = dentry;
    filp->f_pos = 0;
    filp->f_flags = flags;
    filp->private_data = NULL;
    
    // 调用文件系统的 open
    if (inode->i_fop && inode->i_fop->open) {
        int ret = inode->i_fop->open(inode, filp);
        if (ret < 0) {
            printf("[vfs] open: filesystem open failed\n");
            kfree(filp);
            return NULL;
        }
    }
    
    return filp;
}

ssize_t vfs_read(struct file *filp, char *buf, size_t count, loff_t *pos) {
    if (!filp || !filp->f_dentry || !filp->f_dentry->inode) {
        return -EFAULT;
    }
    
    struct inode *inode = filp->f_dentry->inode;
    
    if (!inode->i_fop || !inode->i_fop->read) {
        printf("[vfs] read: no read operation\n");
        return -ENOSYS;
    }
    
    return inode->i_fop->read(filp, buf, count, pos);
}

ssize_t vfs_write(struct file *filp, const char *buf, size_t count, loff_t *pos) {
    if (!filp || !filp->f_dentry || !filp->f_dentry->inode) {
        return -EFAULT;
    }
    
    struct inode *inode = filp->f_dentry->inode;
    
    if (!inode->i_fop || !inode->i_fop->write) {
        printf("[vfs] write: no write operation\n");
        return -ENOSYS;
    }
    
    return inode->i_fop->write(filp, buf, count, pos);
}

int vfs_close(struct file *filp) {
    if (!filp) return 0;
    
    struct inode *inode = filp->f_dentry->inode;
    
    if (inode && inode->i_fop && inode->i_fop->release) {
        inode->i_fop->release(inode, filp);
    }
    
    vfs_put_dentry(filp->f_dentry);
    kfree(filp);
    
    return 0;
}
