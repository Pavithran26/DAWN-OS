/* SPDX-License-Identifier: Unlicense */
#include "iso9660.h"

struct iso9660_fs {
    uint8_t *image;
    size_t size;
    uint32_t block_size;
    uint32_t root_extent;
    uint32_t root_size;
};

static struct iso9660_fs global_iso_state;
static struct vfs_node iso_root_node;
static struct vfs_node iso_file_nodes[16];
static uint32_t iso_node_count;

static inline void iso_str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool iso_str_equal(const char *s1, const char *s2) {
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

static void iso_format_name(const char *raw, uint8_t raw_len, char *out, size_t max_out) {
    if (out == NULL || max_out == 0) return;
    uint8_t len = raw_len;
    for (uint8_t i = 0; i < raw_len; i++) {
        if (raw[i] == ';') {
            len = i;
            break;
        }
    }
    if (len > 0 && raw[len - 1] == '.') len--;
    size_t copy_len = len;
    if (copy_len >= max_out) copy_len = max_out - 1U;
    for (size_t i = 0; i < copy_len; i++) {
        out[i] = raw[i];
    }
    out[copy_len] = '\0';
}

struct iso9660_file_info {
    uint32_t extent_lba;
};

static bool iso9660_extent_valid(const struct iso9660_fs *fs,uint32_t extent,uint64_t length) {
    if (fs==NULL || fs->block_size==0U) return false;
    const uint64_t start=(uint64_t)extent*fs->block_size;
    return start<=fs->size && length<=(uint64_t)fs->size-start;
}

static bool iso9660_directory_extent(const struct vfs_node *node,uint32_t *extent,uint64_t *size) {
    if (node==NULL || extent==NULL || size==NULL || node->type!=VFS_TYPE_DIRECTORY) return false;
    if (node==&iso_root_node) { *extent=global_iso_state.root_extent; *size=global_iso_state.root_size; }
    else {
        if (node->device_or_fs_data==NULL) return false;
        *extent=((struct iso9660_file_info *)node->device_or_fs_data)->extent_lba;
        *size=node->size;
    }
    return iso9660_extent_valid(&global_iso_state,*extent,*size);
}

/* 1=record, 0=end-of-sector padding/end, -1=malformed directory data. */
static int iso9660_get_record(uint32_t extent,uint64_t dir_size,uint64_t offset,
                              struct iso9660_dirent **out_record) {
    struct iso9660_fs *fs=&global_iso_state;
    if (out_record==NULL || offset>=dir_size ||
        !iso9660_extent_valid(fs,extent,dir_size)) return -1;
    const uint64_t base=(uint64_t)extent*fs->block_size;
    const uint64_t remaining=dir_size-offset;
    const uint64_t sector_remaining=fs->block_size-(offset%fs->block_size);
    uint8_t *at=fs->image+base+offset;
    if (at[0]==0U) { *out_record=NULL; return 0; }
    if (remaining<33ULL || sector_remaining<33ULL) return -1;
    struct iso9660_dirent *record=(struct iso9660_dirent *)(uintptr_t)at;
    if (record->length<34U || record->length>remaining ||
        record->length>sector_remaining || record->name_len==0U ||
        (uint16_t)record->name_len>(uint16_t)(record->length-33U)) return -1;
    if (!iso9660_extent_valid(fs,record->extent_lba_le,record->data_length_le)) return -1;
    *out_record=record;
    return 1;
}
static int64_t iso9660_read(struct vfs_node *node,uint64_t offset,size_t size,void *buffer) {
    if (node==NULL || node->type!=VFS_TYPE_FILE || node->device_or_fs_data==NULL ||
        (size!=0U && buffer==NULL)) return -1;
    if (size==0U || offset>=node->size) return 0;
    struct iso9660_file_info *fi=(struct iso9660_file_info *)node->device_or_fs_data;
    struct iso9660_fs *fs=&global_iso_state;
    size_t count=size;
    if ((uint64_t)count>node->size-offset) count=(size_t)(node->size-offset);
    if (!iso9660_extent_valid(fs,fi->extent_lba,node->size)) return -1;
    const uint64_t start=(uint64_t)fi->extent_lba*fs->block_size;
    if (offset>fs->size-start) return -1;
    if ((uint64_t)count>fs->size-start-offset) count=(size_t)(fs->size-start-offset);
    for (size_t i=0U;i<count;i++) ((uint8_t *)buffer)[i]=fs->image[start+offset+i];
    return (int64_t)count;
}

static int iso9660_readdir(struct vfs_node *node,uint32_t index,struct vfs_dirent *out_dirent) {
    uint32_t extent=0U;
    uint64_t dir_size=0ULL,offset=0ULL;
    uint32_t current=0U;
    if (node==NULL || out_dirent==NULL || !iso9660_directory_extent(node,&extent,&dir_size)) return -1;
    while (offset<dir_size) {
        struct iso9660_dirent *record=NULL;
        const int result=iso9660_get_record(extent,dir_size,offset,&record);
        if (result<0) return -1;
        if (result==0) { offset=((offset/global_iso_state.block_size)+1ULL)*global_iso_state.block_size; continue; }
        if (record->name_len>1U || (uint8_t)record->name[0]>1U) {
            if (current==index) {
                iso_format_name(record->name,record->name_len,out_dirent->name,sizeof(out_dirent->name));
                out_dirent->inode=record->extent_lba_le;
                out_dirent->type=(record->file_flags&ISO9660_FLAG_DIRECTORY)?VFS_TYPE_DIRECTORY:VFS_TYPE_FILE;
                out_dirent->size=record->data_length_le;
                return 0;
            }
            ++current;
        }
        offset+=record->length;
    }
    return -1;
}

static struct vfs_node *iso9660_finddir(struct vfs_node *node,const char *name) {
    uint32_t extent=0U;
    uint64_t dir_size=0ULL,offset=0ULL;
    if (node==NULL || name==NULL || !iso9660_directory_extent(node,&extent,&dir_size)) return NULL;
    while (offset<dir_size) {
        struct iso9660_dirent *record=NULL;
        const int result=iso9660_get_record(extent,dir_size,offset,&record);
        if (result<0) return NULL;
        if (result==0) { offset=((offset/global_iso_state.block_size)+1ULL)*global_iso_state.block_size; continue; }
        if (record->name_len>1U || (uint8_t)record->name[0]>1U) {
            char formatted[VFS_MAX_NAME];
            iso_format_name(record->name,record->name_len,formatted,sizeof(formatted));
            if (iso_str_equal(formatted,name)) {
                if (iso_node_count>=16U) return NULL;
                const size_t slot=iso_node_count++;
                struct vfs_node *entry=&iso_file_nodes[slot];
                static struct iso9660_file_info fi_pool[16];
                struct iso9660_file_info *fi=&fi_pool[slot];
                fi->extent_lba=record->extent_lba_le;
                *entry=(struct vfs_node){0};
                iso_str_copy(entry->name,formatted,VFS_MAX_NAME);
                entry->inode=record->extent_lba_le;
                entry->type=(record->file_flags&ISO9660_FLAG_DIRECTORY)?VFS_TYPE_DIRECTORY:VFS_TYPE_FILE;
                entry->size=record->data_length_le;
                entry->permissions=VFS_FLAG_READ;
                entry->device_or_fs_data=fi;
                entry->ops=node->ops;
                entry->parent=node;
                return entry;
            }
        }
        offset+=record->length;
    }
    return NULL;
}
static const struct vfs_operations iso9660_ops = {
    .read = iso9660_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = iso9660_readdir,
    .finddir = iso9660_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL
};

struct vfs_node *iso9660_mount_image(void *image_data,size_t image_size) {
    if (image_data==NULL || image_size<(ISO9660_PVD_SECTOR+1U)*ISO9660_SECTOR_SIZE) return NULL;
    uint8_t *pvd_raw=(uint8_t *)image_data+(ISO9660_PVD_SECTOR*ISO9660_SECTOR_SIZE);
    struct iso9660_pvd *pvd=(struct iso9660_pvd *)(uintptr_t)pvd_raw;
    if (pvd->type!=1U || pvd->id[0]!='C' || pvd->id[1]!='D' || pvd->id[2]!='0' ||
        pvd->id[3]!='0' || pvd->id[4]!='1' || pvd->version!=1U ||
        pvd->logical_block_size_le!=ISO9660_SECTOR_SIZE || pvd->volume_space_size_le==0U) return NULL;
    const uint64_t volume_bytes=(uint64_t)pvd->volume_space_size_le*ISO9660_SECTOR_SIZE;
    if (volume_bytes>image_size) return NULL;
    struct iso9660_dirent *root=(struct iso9660_dirent *)(uintptr_t)pvd->root_directory_record;
    if (root->length<34U || root->name_len!=1U || (uint8_t)root->name[0]!=0U ||
        (root->file_flags&ISO9660_FLAG_DIRECTORY)==0U) return NULL;
    const uint64_t root_start=(uint64_t)root->extent_lba_le*ISO9660_SECTOR_SIZE;
    if (root_start>volume_bytes || root->data_length_le>volume_bytes-root_start) return NULL;
    global_iso_state=(struct iso9660_fs){
        .image=(uint8_t *)image_data,.size=(size_t)volume_bytes,
        .block_size=ISO9660_SECTOR_SIZE,.root_extent=root->extent_lba_le,
        .root_size=root->data_length_le
    };
    iso_node_count=0U;
    iso_root_node=(struct vfs_node){0};
    iso_str_copy(iso_root_node.name,"iso9660",VFS_MAX_NAME);
    iso_root_node.type=VFS_TYPE_DIRECTORY;
    iso_root_node.permissions=VFS_FLAG_READ;
    iso_root_node.size=root->data_length_le;
    iso_root_node.ops=iso9660_ops;
    iso_root_node.inode=root->extent_lba_le;
    return &iso_root_node;
}
#ifdef DAWN_KERNEL_TEST
bool iso9660_self_test(void) {
    static uint8_t image[65536] __attribute__((aligned(16)));
    for (size_t i=0U;i<sizeof(image);i++) image[i]=0U;
    struct iso9660_pvd *pvd=(struct iso9660_pvd *)(uintptr_t)(image+16U*2048U);
    pvd->type=1U; pvd->id[0]='C'; pvd->id[1]='D'; pvd->id[2]='0'; pvd->id[3]='0'; pvd->id[4]='1';
    pvd->version=1U; pvd->volume_space_size_le=32U; pvd->logical_block_size_le=2048U;
    struct iso9660_dirent *root=(struct iso9660_dirent *)(uintptr_t)pvd->root_directory_record;
    root->length=34U; root->extent_lba_le=17U; root->data_length_le=2048U;
    root->file_flags=ISO9660_FLAG_DIRECTORY; root->name_len=1U; root->name[0]=0U;
    uint8_t *dir=image+17U*2048U;
    struct iso9660_dirent *self=(struct iso9660_dirent *)(uintptr_t)dir;
    self->length=34U; self->extent_lba_le=17U; self->data_length_le=2048U;
    self->file_flags=ISO9660_FLAG_DIRECTORY; self->name_len=1U; self->name[0]=0U;
    struct iso9660_dirent *parent=(struct iso9660_dirent *)(uintptr_t)(dir+34U);
    parent->length=34U; parent->extent_lba_le=17U; parent->data_length_le=2048U;
    parent->file_flags=ISO9660_FLAG_DIRECTORY; parent->name_len=1U; parent->name[0]=1U;
    struct iso9660_dirent *file=(struct iso9660_dirent *)(uintptr_t)(dir+68U);
    file->length=44U; file->extent_lba_le=18U; file->data_length_le=15U; file->name_len=10U;
    static const char filename[]="TEST.TXT;1";
    for (size_t i=0U;i<10U;i++) file->name[i]=filename[i];
    static const char contents[]="ISO9660-OK-DATA";
    for (size_t i=0U;i<15U;i++) image[18U*2048U+i]=(uint8_t)contents[i];
    root->extent_lba_le=99U;
    if (iso9660_mount_image(image,sizeof(image))!=NULL) return false;
    root->extent_lba_le=17U;
    struct vfs_node *mounted=iso9660_mount_image(image,sizeof(image));
    if (mounted==NULL) return false;
    struct vfs_node *file_node=iso9660_finddir(mounted,"TEST.TXT");
    if (file_node==NULL || file_node->size!=15U) return false;
    char readback[16]={0};
    if (iso9660_read(file_node,0U,15U,readback)!=15) return false;
    for (size_t i=0U;i<15U;i++) if (readback[i]!=contents[i]) return false;
    struct vfs_dirent entry={0};
    if (iso9660_readdir(mounted,0U,&entry)!=0 || !iso_str_equal(entry.name,"TEST.TXT")) return false;
    return true;
}
#endif
