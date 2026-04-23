// ext4_ops.c — 演示 VFS 如何调用具体文件系统的操作
// 简化版 ext4 文件系统操作实现，演示 VFS 的工作原理
#include "vfs.h"
#include "string.h"
#include "printf.h"
#include "memory.h"

// ============================================================
// ext4 私有数据
// ============================================================

// ext4 超级块（简化版）
struct ext4_super_block {
    u32 s_inodes_count;      // inode 总数
    u32 s_blocks_count;      // 块总数
    u32 s_free_inodes_count; // 空闲 inode 数
    u32 s_free_blocks_count; // 空闲块数
    u32 s_first_data_block;  // 第一个数据块
    u32 s_log_block_size;    // 块大小 log2
    char s_volume_name[16];  // 卷名
};

// ext4 inode（磁盘上的 inode 结构）
struct ext4_inode {
    u16 i_mode;         // 文件类型 + 权限
    u16 i_uid;          // UID
    u32 i_size;        // 文件大小
    u32 i_atime;       // 访问时间
    u32 i_ctime;       // 创建时间
    u32 i_mtime;       // 修改时间
    u32 i_dtime;       // 删除时间
    u16 i_gid;         // GID
    u16 i_links_count; // 硬链接数
    u32 i_blocks;      // 占用块数（512字节为单位）
    u32 i_block[15];   // 数据块指针（直接+间接）
    // ...
};

// ============================================================
// ext4 文件系统私有数据
// ============================================================

struct ext4_fs_info {
    struct ext4_super_block *sb;       // 超级块
    struct ext4_inode *inode_table;    // inode 表（内存中）
    struct dentry *root_dentry;        // 根目录 dentry
    char *block_bitmap;                // 块位图
    char *inode_bitmap;                // inode 位图
};

// ============================================================
// ext4 文件操作
// ============================================================

static int ext4_open(struct inode *inode, struct file *filp) {
    printf("[ext4] open: ino=%lu\n", inode->ino);
    filp->private_data = NULL;
    return 0;
}

static ssize_t ext4_read(struct file *filp, char *buf, size_t count, loff_t *pos) {
    struct inode *inode = filp->f_dentry->inode;
    struct ext4_fs_info *fsi = inode->i_sb->s_fs_info;
    
    printf("[ext4] read: ino=%lu pos=%lld count=%zu\n", inode->ino, *pos, count);
    
    if (*pos >= inode->i_size) {
        return 0;  // EOF
    }
    
    if (*pos + count > inode->i_size) {
        count = inode->i_size - *pos;
    }
    
    // 简化：假设数据全在第一个直接块里
    if (inode->i_block[0]) {
        char *data = (char *)(fsi->inode_table[inode->ino % 128].i_block[0] * 4096);
        memcpy(buf, data + *pos, count);
    } else {
        memset(buf, 0, count);
    }
    
    *pos += count;
    return count;
}

static ssize_t ext4_write(struct file *filp, const char *buf, size_t count, loff_t *pos) {
    struct inode *inode = filp->f_dentry->inode;
    
    printf("[ext4] write: ino=%lu pos=%lld count=%zu\n", inode->ino, *pos, count);
    
    if (*pos + count > inode->i_size) {
        inode->i_size = *pos + count;
    }
    
    *pos += count;
    return count;
}

static int ext4_release(struct inode *inode, struct file *filp) {
    printf("[ext4] release: ino=%lu\n", inode->ino);
    return 0;
}

static int ext4_fsync(struct inode *inode, int datasync) {
    printf("[ext4] fsync: ino=%lu\n", inode->ino);
    // 触发 writeback
    return 0;
}

// ============================================================
// ext4 inode 操作
// ============================================================

static int ext4_create(struct inode *dir, const char *name, mode_t mode, struct dentry **dentry) {
    struct inode *inode;
    struct ext4_fs_info *fsi = dir->i_sb->s_fs_info;
    
    printf("[ext4] create: '%s' in dir=%lu\n", name, dir->ino);
    
    // 分配新 inode
    inode = vfs_new_inode(dir->i_sb, S_IFREG | mode);
    if (!inode) return -ENOMEM;
    
    // 初始化 ext4 inode
    int idx = inode->ino % 128;
    fsi->inode_table[idx].i_mode = inode->i_mode;
    fsi->inode_table[idx].i_size = 0;
    fsi->inode_table[idx].i_links_count = 1;
    
    // 设置 VFS 操作
    inode->i_fop = ext4_file_operations;
    
    // 创建 dentry
    *dentry = vfs_new_dentry(name, inode);
    
    return 0;
}

static struct dentry *ext4_lookup(struct inode *dir, const char *name, struct dentry **dentry) {
    struct ext4_fs_info *fsi = dir->i_sb->s_fs_info;
    
    printf("[ext4] lookup: '%s' in dir=%lu\n", name, dir->ino);
    
    // 简化：遍历 inode 表查找
    for (int i = 1; i < 128; i++) {
        if (fsi->inode_table[i].i_mode != 0) {
            // 这里应该读目录文件内容来匹配名字
            // 简化：假设名字就是 inode 编号
        }
    }
    
    return NULL;
}

static int ext4_mkdir(struct inode *dir, const char *name, mode_t mode) {
    struct inode *inode;
    
    printf("[ext4] mkdir: '%s' in dir=%lu\n", name, dir->ino);
    
    inode = vfs_new_inode(dir->i_sb, S_IFDIR | mode);
    if (!inode) return -ENOMEM;
    
    inode->i_op = ext4_dir_inode_operations;
    inode->i_fop = ext4_dir_file_operations;
    
    return 0;
}

// ============================================================
// 操作表定义
// ============================================================

struct file_operations ext4_file_operations = {
    .open    = ext4_open,
    .read    = ext4_read,
    .write   = ext4_write,
    .release = ext4_release,
    .fsync   = ext4_fsync,
};

struct inode_operations ext4_file_inode_operations = {
    .create = ext4_create,
    .lookup = ext4_lookup,
};

struct inode_operations ext4_dir_inode_operations = {
    .create = ext4_create,
    .lookup = ext4_lookup,
    .mkdir  = ext4_mkdir,
};

struct file_operations ext4_dir_file_operations = {
    .open    = ext4_open,
    .read    = ext4_read,  // read 目录内容
    .release = ext4_release,
};

// ============================================================
// ext4 mount
// ============================================================

static int ext4_mount(struct super_block *sb, const char *mnt_point, void *data) {
    struct ext4_fs_info *fsi;
    
    printf("[ext4] mounting at '%s'\n", mnt_point);
    
    // 分配文件系统私有数据
    fsi = kmalloc(sizeof(struct ext4_fs_info), GFP_KERNEL);
    if (!fsi) return -ENOMEM;
    
    memset(fsi, 0, sizeof(struct ext4_fs_info));
    sb->s_fs_info = fsi;
    
    // 分配超级块
    fsi->sb = kmalloc(sizeof(struct ext4_super_block), GFP_KERNEL);
    memset(fsi->sb, 0, sizeof(struct ext4_super_block));
    fsi->sb->s_inodes_count = 1024;
    fsi->sb->s_blocks_count = 65536;
    fsi->sb->s_free_inodes_count = 1000;
    fsi->sb->s_free_blocks_count = 60000;
    
    // 分配 inode 表（简化：内存中）
    fsi->inode_table = kmalloc(128 * sizeof(struct ext4_inode), GFP_KERNEL);
    memset(fsi->inode_table, 0, 128 * sizeof(struct ext4_inode));
    
    // 创建根目录 inode
    struct inode *root_inode = vfs_new_inode(sb, S_IFDIR | 0755);
    root_inode->i_op = &ext4_dir_inode_operations;
    root_inode->i_fop = &ext4_dir_file_operations;
    fsi->inode_table[0].i_mode = root_inode->i_mode;
    
    sb->s_root_inode = root_inode;
    
    // 创建根目录 dentry
    fsi->root_dentry = vfs_new_dentry("/", root_inode);
    vfs_root_dentry = fsi->root_dentry;
    
    printf("[ext4] mounted successfully\n");
    return 0;
}

// ============================================================
// ext4 文件系统类型注册
// ============================================================

struct filesystem_type ext4_fs_type = {
    .name   = "ext4",
    .mount  = ext4_mount,
};

int ext4_init(void) {
    return register_filesystem(&ext4_fs_type);
}
