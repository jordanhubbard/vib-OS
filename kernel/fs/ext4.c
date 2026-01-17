/*
 * vib-OS Kernel - ext4 Filesystem Driver
 * 
 * Read/write support for ext4 filesystem.
 */

#include "fs/vfs.h"
#include "printk.h"
#include "mm/kmalloc.h"
#include "types.h"

/* ===================================================================== */
/* ext4 Constants */
/* ===================================================================== */

#define EXT4_SUPER_MAGIC    0xEF53
#define EXT4_SUPERBLOCK_OFFSET  1024
#define EXT4_BLOCK_SIZE_MIN     1024
#define EXT4_BLOCK_SIZE_MAX     65536

#define EXT4_GOOD_OLD_REV       0
#define EXT4_DYNAMIC_REV        1

#define EXT4_FEATURE_INCOMPAT_64BIT         0x0080
#define EXT4_FEATURE_INCOMPAT_EXTENTS       0x0040
#define EXT4_FEATURE_INCOMPAT_FLEX_BG       0x0200

#define EXT4_S_IFREG    0x8000
#define EXT4_S_IFDIR    0x4000
#define EXT4_S_IFLNK    0xA000

#define EXT4_NDIR_BLOCKS    12
#define EXT4_IND_BLOCK      EXT4_NDIR_BLOCKS
#define EXT4_DIND_BLOCK     (EXT4_IND_BLOCK + 1)
#define EXT4_TIND_BLOCK     (EXT4_DIND_BLOCK + 1)
#define EXT4_N_BLOCKS       (EXT4_TIND_BLOCK + 1)

/* ===================================================================== */
/* ext4 On-disk Structures */
/* ===================================================================== */

struct ext4_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* Dynamic rev */
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    /* Performance hints */
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    /* Journaling */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    /* 64-bit support */
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint16_t s_raid_stride;
    uint16_t s_mmp_interval;
    uint64_t s_mmp_block;
    uint32_t s_raid_stripe_width;
    uint8_t  s_log_groups_per_flex;
    uint8_t  s_checksum_type;
    uint16_t s_reserved_pad;
    uint64_t s_kbytes_written;
    /* ... more fields */
} __attribute__((packed));

struct ext4_group_desc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    /* 64-bit support */
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} __attribute__((packed));

struct ext4_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT4_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_hi;
    uint32_t i_obso_faddr;
    /* ... more fields */
} __attribute__((packed));

struct ext4_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} __attribute__((packed));

/* ===================================================================== */
/* ext4 In-memory Structures */
/* ===================================================================== */

struct ext4_fs {
    struct ext4_superblock sb;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t desc_size;
    struct ext4_group_desc *group_descs;
    void *device;  /* Block device */
    /* Read function */
    int (*read_block)(void *device, uint64_t block, void *buf);
    int (*write_block)(void *device, uint64_t block, const void *buf);
};

/* ===================================================================== */
/* ext4 Functions */
/* ===================================================================== */

static int ext4_read_block(struct ext4_fs *fs, uint64_t block, void *buf)
{
    if (fs->read_block) {
        return fs->read_block(fs->device, block, buf);
    }
    return -1;
}

static int ext4_read_inode(struct ext4_fs *fs, uint32_t ino, struct ext4_inode *inode)
{
    if (ino == 0) return -1;
    
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    struct ext4_group_desc *gd = &fs->group_descs[group];
    uint64_t inode_table = gd->bg_inode_table_lo;
    if (fs->desc_size >= 64) {
        inode_table |= ((uint64_t)gd->bg_inode_table_hi << 32);
    }
    
    uint64_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint64_t offset_in_block = (index * fs->inode_size) % fs->block_size;
    
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -1;
    
    if (ext4_read_block(fs, inode_table + block_offset, buf) < 0) {
        kfree(buf);
        return -1;
    }
    
    /* Copy inode */
    uint8_t *src = buf + offset_in_block;
    uint8_t *dst = (uint8_t *)inode;
    for (size_t i = 0; i < sizeof(struct ext4_inode); i++) {
        dst[i] = src[i];
    }
    
    kfree(buf);
    return 0;
}

static uint64_t ext4_get_file_block(struct ext4_fs *fs, struct ext4_inode *inode, 
                                     uint64_t file_block)
{
    /* Direct blocks */
    if (file_block < EXT4_NDIR_BLOCKS) {
        return inode->i_block[file_block];
    }
    
    /* TODO: Implement indirect blocks */
    /* TODO: Implement extent tree */
    
    return 0;
}

static int ext4_read_file(struct ext4_fs *fs, uint32_t ino, void *buf, 
                           size_t offset, size_t len)
{
    struct ext4_inode inode;
    
    if (ext4_read_inode(fs, ino, &inode) < 0) {
        return -1;
    }
    
    uint64_t file_size = inode.i_size_lo;
    if (offset >= file_size) return 0;
    if (offset + len > file_size) {
        len = file_size - offset;
    }
    
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    size_t bytes_read = 0;
    
    while (bytes_read < len) {
        uint64_t file_block = (offset + bytes_read) / fs->block_size;
        uint64_t block_offset = (offset + bytes_read) % fs->block_size;
        
        uint64_t disk_block = ext4_get_file_block(fs, &inode, file_block);
        if (disk_block == 0) break;
        
        if (ext4_read_block(fs, disk_block, block_buf) < 0) {
            break;
        }
        
        size_t to_copy = fs->block_size - block_offset;
        if (to_copy > len - bytes_read) {
            to_copy = len - bytes_read;
        }
        
        for (size_t i = 0; i < to_copy; i++) {
            ((uint8_t *)buf)[bytes_read + i] = block_buf[block_offset + i];
        }
        
        bytes_read += to_copy;
    }
    
    kfree(block_buf);
    return bytes_read;
}

/* ===================================================================== */
/* VFS Integration */
/* ===================================================================== */

static struct ext4_fs *root_ext4 = NULL;

int ext4_mount(void *device, 
               int (*read_block)(void*, uint64_t, void*),
               int (*write_block)(void*, uint64_t, const void*))
{
    printk(KERN_INFO "EXT4: Mounting filesystem\n");
    
    struct ext4_fs *fs = kmalloc(sizeof(struct ext4_fs));
    if (!fs) return -1;
    
    fs->device = device;
    fs->read_block = read_block;
    fs->write_block = write_block;
    
    /* Read superblock */
    uint8_t sb_buf[1024];
    
    /* Superblock is at offset 1024 */
    if (read_block(device, 1, sb_buf) < 0) {
        kfree(fs);
        return -1;
    }
    
    /* Copy superblock */
    uint8_t *src = sb_buf;
    uint8_t *dst = (uint8_t *)&fs->sb;
    for (size_t i = 0; i < sizeof(struct ext4_superblock); i++) {
        dst[i] = src[i];
    }
    
    /* Verify magic */
    if (fs->sb.s_magic != EXT4_SUPER_MAGIC) {
        printk(KERN_ERR "EXT4: Invalid magic number\n");
        kfree(fs);
        return -1;
    }
    
    /* Calculate parameters */
    fs->block_size = 1024 << fs->sb.s_log_block_size;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->inode_size = (fs->sb.s_rev_level >= EXT4_DYNAMIC_REV) 
                     ? fs->sb.s_inode_size : 128;
    
    uint64_t total_blocks = fs->sb.s_blocks_count_lo;
    if (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
        total_blocks |= ((uint64_t)fs->sb.s_blocks_count_hi << 32);
    }
    
    fs->group_count = (total_blocks + fs->blocks_per_group - 1) / fs->blocks_per_group;
    fs->desc_size = (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) 
                    ? fs->sb.s_desc_size : 32;
    
    printk(KERN_INFO "EXT4: Block size: %u\n", fs->block_size);
    printk(KERN_INFO "EXT4: Groups: %u\n", fs->group_count);
    printk(KERN_INFO "EXT4: Volume: %s\n", fs->sb.s_volume_name);
    
    /* Read group descriptors */
    size_t gd_size = fs->group_count * fs->desc_size;
    fs->group_descs = kmalloc(gd_size);
    if (!fs->group_descs) {
        kfree(fs);
        return -1;
    }
    
    uint64_t gd_block = (fs->block_size == 1024) ? 2 : 1;
    uint8_t *gd_buf = kmalloc(fs->block_size);
    
    /* TODO: Read all group descriptors */
    
    if (gd_buf) kfree(gd_buf);
    
    root_ext4 = fs;
    printk(KERN_INFO "EXT4: Filesystem mounted successfully\n");
    
    return 0;
}

int ext4_unmount(void)
{
    if (root_ext4) {
        if (root_ext4->group_descs) {
            kfree(root_ext4->group_descs);
        }
        kfree(root_ext4);
        root_ext4 = NULL;
    }
    return 0;
}
