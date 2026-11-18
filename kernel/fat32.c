/* SPDX-License-Identifier: Unlicense */
#include "fat32.h"

struct fat32_fs {
    uint8_t *image;
    size_t size;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t data_start_sector;
    uint32_t total_sectors;
    uint32_t cluster_count;
};

static struct fat32_fs global_fat32_state;
static struct vfs_node fat32_root_node;
static struct vfs_node fat32_file_nodes[16];
static uint32_t fat32_node_count;

static inline void fat_str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool fat_str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 'a' + 'A');
        if (c2 >= 'a' && c2 <= 'z') c2 = (char)(c2 - 'a' + 'A');
        if (c1 != c2) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static uint32_t fat32_get_fat_entry(struct fat32_fs *fs,uint32_t cluster) {
    if (fs==NULL || fs->bytes_per_sector==0U || cluster<2U || cluster>fs->cluster_count+1U) return 0x0ffffff7U;
    const uint64_t fat_offset=(uint64_t)cluster*4ULL;
    const uint64_t fat_bytes=(uint64_t)fs->fat_size_sectors*fs->bytes_per_sector;
    const uint64_t fat_base=(uint64_t)fs->reserved_sectors*fs->bytes_per_sector;
    if (fat_offset>fat_bytes || fat_bytes-fat_offset<4ULL || fat_base>fs->size ||
        fat_offset>fs->size-fat_base || fs->size-(fat_base+fat_offset)<4ULL) return 0x0ffffff7U;
    const uint8_t *p=fs->image+fat_base+fat_offset;
    return ((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24))&0x0fffffffU;
}

static uint8_t *fat32_get_cluster_ptr(struct fat32_fs *fs,uint32_t cluster) {
    if (fs==NULL || cluster<2U || cluster>fs->cluster_count+1U || fs->sectors_per_cluster==0U) return NULL;
    const uint64_t sector=(uint64_t)fs->data_start_sector+(uint64_t)(cluster-2U)*fs->sectors_per_cluster;
    if (sector>fs->total_sectors || fs->sectors_per_cluster>fs->total_sectors-sector) return NULL;
    const uint64_t byte_pos=sector*fs->bytes_per_sector;
    if (byte_pos>fs->size || fs->cluster_size>fs->size-byte_pos) return NULL;
    return fs->image+byte_pos;
}
static void fat32_format_short_name(const char raw[11], char *out) {
    int out_idx = 0;
    for (int i = 0; i < 8; i++) {
        if (raw[i] != ' ') out[out_idx++] = raw[i];
    }
    if (raw[8] != ' ') {
        out[out_idx++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] != ' ') out[out_idx++] = raw[i];
        }
    }
    out[out_idx] = '\0';
}

struct fat32_file_info {
    uint32_t first_cluster;
    uint32_t dir_cluster;
    uint32_t dir_offset;
};

static int64_t fat32_read(struct vfs_node *node,uint64_t offset,size_t size,void *buffer) {
    if (node==NULL || node->type!=VFS_TYPE_FILE || node->device_or_fs_data==NULL || (size!=0U && buffer==NULL)) return -1;
    if (size==0U || offset>=node->size) return 0;
    struct fat32_file_info *fi=(struct fat32_file_info *)node->device_or_fs_data;
    struct fat32_fs *fs=&global_fat32_state;
    size_t to_read=size;
    if ((uint64_t)to_read>node->size-offset) to_read=(size_t)(node->size-offset);
    if (fi->first_cluster<2U || fi->first_cluster>fs->cluster_count+1U) return -1;
    uint32_t cluster=fi->first_cluster,steps=0U;
    uint64_t skip=offset;
    while (skip>=fs->cluster_size) {
        if (steps++>=fs->cluster_count) return -1;
        const uint32_t next=fat32_get_fat_entry(fs,cluster);
        if (next>=0x0ffffff8U || next<2U || next>fs->cluster_count+1U) return -1;
        cluster=next; skip-=fs->cluster_size;
    }
    size_t copied=0U;
    while (copied<to_read && steps<fs->cluster_count) {
        uint8_t *cluster_data=fat32_get_cluster_ptr(fs,cluster);
        if (cluster_data==NULL) break;
        size_t chunk=fs->cluster_size-(size_t)skip;
        if (chunk>to_read-copied) chunk=to_read-copied;
        for (size_t i=0U;i<chunk;i++) ((uint8_t *)buffer)[copied+i]=cluster_data[(size_t)skip+i];
        copied+=chunk; skip=0U; ++steps;
        if (copied<to_read) {
            const uint32_t next=fat32_get_fat_entry(fs,cluster);
            if (next>=0x0ffffff8U || next<2U || next>fs->cluster_count+1U) break;
            cluster=next;
        }
    }
    return (int64_t)copied;
}
static int fat32_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent) {
    if (node == NULL || out_dirent == NULL) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    uint32_t cluster=0U;
    uint32_t current_idx=0U,steps=0U;
    if (node->type!=VFS_TYPE_DIRECTORY ||
        (node!=&fat32_root_node && node->device_or_fs_data==NULL)) return -1;
    cluster=(node==&fat32_root_node)?fs->root_cluster:
        ((struct fat32_file_info *)node->device_or_fs_data)->first_cluster;
    while (cluster>=2U && cluster<0x0ffffff8U && steps++<fs->cluster_count) {
        uint8_t *data = fat32_get_cluster_ptr(fs, cluster);
        if (data == NULL) break;
        uint32_t entries = fs->cluster_size / sizeof(struct fat32_dirent);
        struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)data;

        for (uint32_t i = 0; i < entries; i++) {
            if ((uint8_t)d[i].name[0] == 0x00U) return -1; /* End */
            if ((uint8_t)d[i].name[0] == 0xe5U) continue;
            if (d[i].attributes==FAT32_ATTR_LFN ||
                (d[i].attributes&FAT32_ATTR_VOLUME_ID)!=0U ||
                d[i].name[0]=='.') continue;

            if (current_idx == index) {
                fat32_format_short_name(d[i].name, out_dirent->name);
                out_dirent->inode = ((uint32_t)d[i].first_cluster_high << 16) | d[i].first_cluster_low;
                out_dirent->type = (d[i].attributes & FAT32_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
                out_dirent->size = d[i].file_size;
                return 0;
            }
            current_idx++;
        }
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    return -1;
}

static struct vfs_node *fat32_finddir(struct vfs_node *node, const char *name) {
    if (node == NULL || name == NULL) return NULL;
    struct fat32_fs *fs = &global_fat32_state;
    uint32_t cluster=0U;
    uint32_t steps=0U;
    if (node->type!=VFS_TYPE_DIRECTORY ||
        (node!=&fat32_root_node && node->device_or_fs_data==NULL)) return NULL;
    cluster=(node==&fat32_root_node)?fs->root_cluster:
        ((struct fat32_file_info *)node->device_or_fs_data)->first_cluster;
    while (cluster>=2U && cluster<0x0ffffff8U && steps++<fs->cluster_count) {
        uint8_t *data = fat32_get_cluster_ptr(fs, cluster);
        if (data == NULL) break;
        uint32_t entries = fs->cluster_size / sizeof(struct fat32_dirent);
        struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)data;

        for (uint32_t i = 0; i < entries; i++) {
            if ((uint8_t)d[i].name[0] == 0x00U) return NULL;
            if ((uint8_t)d[i].name[0] == 0xe5U) continue;
            if (d[i].attributes==FAT32_ATTR_LFN ||
                (d[i].attributes&FAT32_ATTR_VOLUME_ID)!=0U ||
                d[i].name[0]=='.') continue;

            char formatted[16];
            fat32_format_short_name(d[i].name, formatted);
            if (fat_str_equal(formatted, name)) {
                if (fat32_node_count>=16U) return NULL;
                const size_t slot=fat32_node_count++;
                struct vfs_node *n=&fat32_file_nodes[slot];
                static struct fat32_file_info fi_pool[16];
                struct fat32_file_info *fi=&fi_pool[slot];
                fi->first_cluster = ((uint32_t)d[i].first_cluster_high << 16) | d[i].first_cluster_low;
                fi->dir_cluster = cluster;
                fi->dir_offset = i * sizeof(struct fat32_dirent);
                *n = (struct vfs_node){0};
                fat_str_copy(n->name, formatted, VFS_MAX_NAME);
                n->inode = fi->first_cluster;
                n->type = (d[i].attributes & FAT32_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
                n->size = d[i].file_size;
                n->permissions = VFS_FLAG_READ;
                n->device_or_fs_data = fi;
                n->ops = node->ops;
                n->parent = node;
                return n;
            }
        }
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    return NULL;
}

static const struct vfs_operations fat32_ops = {
    .read = fat32_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = fat32_readdir,
    .finddir = fat32_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL
};

struct vfs_node *fat32_mount_image(void *image_data,size_t image_size) {
    if (image_data==NULL || image_size<512U) return NULL;
    const uint8_t *boot=(const uint8_t *)image_data;
    if (boot[510]!=0x55U || boot[511]!=0xaaU) return NULL;
    struct fat32_bpb *bpb=(struct fat32_bpb *)image_data;
    const uint32_t bps=bpb->bytes_per_sector,spc=bpb->sectors_per_cluster;
    const uint32_t total=bpb->total_sectors_16!=0U?bpb->total_sectors_16:bpb->total_sectors_32;
    if ((bps!=512U && bps!=1024U && bps!=2048U && bps!=4096U) ||
        spc==0U || spc>128U || (spc&(spc-1U))!=0U ||
        bpb->reserved_sector_count==0U || bpb->table_count==0U || bpb->table_count>4U ||
        bpb->table_size_32==0U || total==0U || bpb->root_cluster<2U) return NULL;
    const uint64_t data_start=(uint64_t)bpb->reserved_sector_count+(uint64_t)bpb->table_count*bpb->table_size_32;
    if (data_start>=total || data_start>UINT32_MAX) return NULL;
    const uint64_t total_bytes=(uint64_t)total*bps;
    if (total_bytes>image_size) return NULL;
    const uint64_t clusters=((uint64_t)total-data_start)/spc;
    if (clusters==0ULL || clusters>UINT32_MAX-1ULL || bpb->root_cluster>clusters+1ULL) return NULL;
    const uint64_t fat_entries=((uint64_t)bpb->table_size_32*bps)/4ULL;
    if (fat_entries<clusters+2ULL) return NULL;
    const uint64_t cluster_size=(uint64_t)bps*spc;
    if (cluster_size==0ULL || cluster_size>65536ULL) return NULL;
    global_fat32_state=(struct fat32_fs){
        .image=(uint8_t *)image_data,.size=image_size,.bytes_per_sector=bps,
        .sectors_per_cluster=spc,.cluster_size=(uint32_t)cluster_size,
        .reserved_sectors=bpb->reserved_sector_count,.fat_count=bpb->table_count,
        .fat_size_sectors=bpb->table_size_32,.root_cluster=bpb->root_cluster,
        .data_start_sector=(uint32_t)data_start,.total_sectors=total,
        .cluster_count=(uint32_t)clusters
    };
    fat32_node_count=0U;
    if (fat32_get_cluster_ptr(&global_fat32_state,bpb->root_cluster)==NULL) return NULL;
    fat32_root_node=(struct vfs_node){0};
    fat_str_copy(fat32_root_node.name,"fat32",VFS_MAX_NAME);
    fat32_root_node.type=VFS_TYPE_DIRECTORY;
    fat32_root_node.permissions=VFS_FLAG_READ;
    fat32_root_node.ops=fat32_ops;
    fat32_root_node.inode=bpb->root_cluster;
    return &fat32_root_node;
}
#ifdef DAWN_KERNEL_TEST
bool fat32_self_test(void) {
    /* Construct a minimal 64KB synthetic FAT32 image in memory */
    static uint8_t image[65536] __attribute__((aligned(16)));
    for (size_t i = 0; i < sizeof(image); i++) image[i] = 0;

    struct fat32_bpb *bpb = (struct fat32_bpb *)(uintptr_t)image;
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 1;
    bpb->reserved_sector_count = 32;
    bpb->table_count = 2;
    bpb->table_size_32 = 8;
    bpb->total_sectors_32 = 128;
    bpb->root_cluster = 2;
    image[510]=0x55U; image[511]=0xaaU;

    /* Initialize FAT entries for root cluster (2) and file cluster (3) */
    uint32_t *fat = (uint32_t *)(uintptr_t)(image + (32 * 512));
    fat[0] = 0x0ffffff8U;
    fat[1] = 0x0fffffffU;
    fat[2] = 0x0ffffff8U; /* Root cluster EOC */
    fat[3] = 0x0ffffff8U; /* File cluster EOC */

    /* Put a directory entry in root cluster (cluster 2 -> data sector 0) */
    uint32_t data_start = 32 + (2 * 8);
    struct fat32_dirent *root_entries = (struct fat32_dirent *)(uintptr_t)(image + (data_start * 512));
    for (int i = 0; i < 8; i++) root_entries[0].name[i] = ' ';
    root_entries[0].name[0] = 'T';
    root_entries[0].name[1] = 'E';
    root_entries[0].name[2] = 'S';
    root_entries[0].name[3] = 'T';
    root_entries[0].name[8] = 'T';
    root_entries[0].name[9] = 'X';
    root_entries[0].name[10] = 'T';
    root_entries[0].attributes = FAT32_ATTR_ARCHIVE;
    root_entries[0].first_cluster_low = 3;
    root_entries[0].file_size = 14;

    /* Write data into cluster 3 (data sector 1) */
    uint8_t *file_data = image + ((data_start + 1) * 512);
    static const char test_content[] = "FAT32-OK-DATA!";
    for (size_t i = 0; i < 14; i++) file_data[i] = (uint8_t)test_content[i];

    /* Mount synthetic image */
    struct vfs_node *root = fat32_mount_image(image, sizeof(image));
    if (root == NULL) return false;

    /* Find TEST.TXT */
    struct vfs_node *file_node = fat32_finddir(root, "TEST.TXT");
    if (file_node == NULL || file_node->size != 14) return false;

    /* Read back file */
    char buf[16] = {0};
    if (fat32_read(file_node, 0, 14, buf) != 14) return false;
    for (int i = 0; i < 14; i++) {
        if (buf[i] != test_content[i]) return false;
    }

    /* FAT32 writes are intentionally disabled until allocation, chain growth,
     * mirrored FAT updates, and directory-entry transactions are implemented. */
    return true;
}
#endif
