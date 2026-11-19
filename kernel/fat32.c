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

struct fat32_file_info {
    uint32_t first_cluster;
    uint32_t dir_cluster;
    uint32_t dir_offset;
};

#define FAT32_MAX_NODES 32U
static struct fat32_fs global_fat32_state;
static struct vfs_node fat32_root_node;
static struct vfs_node fat32_file_nodes[FAT32_MAX_NODES];
static struct fat32_file_info fat32_fi_pool[FAT32_MAX_NODES];
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

static uint32_t fat32_get_fat_entry(struct fat32_fs *fs, uint32_t cluster) {
    if (fs == NULL || fs->bytes_per_sector == 0U || cluster < 2U || cluster > fs->cluster_count + 1U)
        return 0x0ffffff7U;
    const uint64_t fat_offset = (uint64_t)cluster * 4ULL;
    const uint64_t fat_bytes = (uint64_t)fs->fat_size_sectors * fs->bytes_per_sector;
    const uint64_t fat_base = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector;
    if (fat_offset > fat_bytes || fat_bytes - fat_offset < 4ULL || fat_base > fs->size ||
        fat_offset > fs->size - fat_base || fs->size - (fat_base + fat_offset) < 4ULL)
        return 0x0ffffff7U;
    const uint8_t *p = fs->image + fat_base + fat_offset;
    return ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)) & 0x0fffffffU;
}

static bool fat32_set_fat_entry(struct fat32_fs *fs, uint32_t cluster, uint32_t value) {
    if (fs == NULL || fs->bytes_per_sector == 0U || cluster < 2U || cluster > fs->cluster_count + 1U)
        return false;
    const uint64_t fat_offset = (uint64_t)cluster * 4ULL;
    const uint64_t fat_bytes = (uint64_t)fs->fat_size_sectors * fs->bytes_per_sector;
    if (fat_offset > fat_bytes || fat_bytes - fat_offset < 4ULL)
        return false;

    /* Mirror the update across all FAT tables */
    for (uint32_t f = 0; f < fs->fat_count; f++) {
        const uint64_t fat_base = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector +
                                  (uint64_t)f * (uint64_t)fs->fat_size_sectors * fs->bytes_per_sector;
        if (fat_base > fs->size || fat_offset > fs->size - fat_base ||
            fs->size - (fat_base + fat_offset) < 4ULL)
            return false;
        uint8_t *p = fs->image + fat_base + fat_offset;
        uint32_t cur = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        uint32_t entry = (cur & 0xF0000000U) | (value & 0x0FFFFFFFU);
        p[0] = (uint8_t)(entry & 0xFFU);
        p[1] = (uint8_t)((entry >> 8) & 0xFFU);
        p[2] = (uint8_t)((entry >> 16) & 0xFFU);
        p[3] = (uint8_t)((entry >> 24) & 0xFFU);
    }
    return true;
}

static uint8_t *fat32_get_cluster_ptr(struct fat32_fs *fs, uint32_t cluster) {
    if (fs == NULL || cluster < 2U || cluster > fs->cluster_count + 1U || fs->sectors_per_cluster == 0U)
        return NULL;
    const uint64_t sector = (uint64_t)fs->data_start_sector + (uint64_t)(cluster - 2U) * fs->sectors_per_cluster;
    if (sector > fs->total_sectors || fs->sectors_per_cluster > fs->total_sectors - sector)
        return NULL;
    const uint64_t byte_pos = sector * fs->bytes_per_sector;
    if (byte_pos > fs->size || fs->cluster_size > fs->size - byte_pos)
        return NULL;
    return fs->image + byte_pos;
}

static uint32_t fat32_alloc_free_cluster(struct fat32_fs *fs) {
    if (fs == NULL) return 0U;
    for (uint32_t c = 2U; c <= fs->cluster_count + 1U; c++) {
        uint32_t entry = fat32_get_fat_entry(fs, c);
        if (entry == 0x00000000U) {
            if (!fat32_set_fat_entry(fs, c, 0x0FFFFFFFU)) return 0U;
            uint8_t *data = fat32_get_cluster_ptr(fs, c);
            if (data != NULL) {
                for (size_t i = 0; i < fs->cluster_size; i++) data[i] = 0;
            }
            return c;
        }
    }
    return 0U;
}

static uint32_t fat32_extend_chain(struct fat32_fs *fs, uint32_t last_cluster) {
    if (fs == NULL) return 0U;
    uint32_t new_cluster = fat32_alloc_free_cluster(fs);
    if (new_cluster == 0U) return 0U;
    if (last_cluster >= 2U && last_cluster <= fs->cluster_count + 1U) {
        if (!fat32_set_fat_entry(fs, last_cluster, new_cluster)) {
            (void)fat32_set_fat_entry(fs, new_cluster, 0x00000000U);
            return 0U;
        }
    }
    return new_cluster;
}

static void fat32_free_chain(struct fat32_fs *fs, uint32_t start_cluster) {
    if (fs == NULL || start_cluster < 2U || start_cluster > fs->cluster_count + 1U) return;
    uint32_t cluster = start_cluster;
    uint32_t steps = 0U;
    while (cluster >= 2U && cluster <= fs->cluster_count + 1U && steps++ < fs->cluster_count) {
        uint32_t next = fat32_get_fat_entry(fs, cluster);
        (void)fat32_set_fat_entry(fs, cluster, 0x00000000U);
        if (next >= 0x0FFFFFF8U || next < 2U || next > fs->cluster_count + 1U) break;
        cluster = next;
    }
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

static bool fat32_to_short_name(const char *name, char out_short[11]) {
    if (name == NULL || name[0] == '\0' || name[0] == ' ') return false;
    for (int i = 0; i < 11; i++) out_short[i] = ' ';
    size_t i = 0;
    size_t base_len = 0;
    while (name[i] != '\0' && name[i] != '.') {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '/' || c == '\\' || c == ' ' || (uint8_t)c < 0x20U || base_len >= 8) return false;
        out_short[base_len++] = c;
        i++;
    }
    if (base_len == 0) return false;
    if (name[i] == '.') {
        i++;
        size_t ext_len = 0;
        while (name[i] != '\0') {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c == '/' || c == '\\' || c == ' ' || c == '.' || (uint8_t)c < 0x20U || ext_len >= 3) return false;
            out_short[8 + ext_len++] = c;
            i++;
        }
    }
    return true;
}

static struct vfs_node *fat32_alloc_node(struct fat32_file_info **out_fi) {
    const size_t slot = fat32_node_count % FAT32_MAX_NODES;
    fat32_node_count++;
    struct vfs_node *n = &fat32_file_nodes[slot];
    *n = (struct vfs_node){0};
    fat32_fi_pool[slot] = (struct fat32_file_info){0};
    if (out_fi != NULL) *out_fi = &fat32_fi_pool[slot];
    return n;
}

static bool fat32_update_dirent_meta(struct fat32_fs *fs, const struct fat32_file_info *fi,
                                     uint32_t first_cluster, uint32_t file_size) {
    if (fs == NULL || fi == NULL || fi->dir_cluster < 2U) return false;
    uint8_t *dir_data = fat32_get_cluster_ptr(fs, fi->dir_cluster);
    if (dir_data == NULL || (size_t)fi->dir_offset + sizeof(struct fat32_dirent) > fs->cluster_size) return false;
    struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)(dir_data + fi->dir_offset);
    d->first_cluster_low = (uint16_t)(first_cluster & 0xFFFFU);
    d->first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFFU);
    d->file_size = file_size;
    return true;
}

static bool fat32_find_free_dirent(struct fat32_fs *fs, uint32_t dir_start_cluster,
                                   uint32_t *out_cluster, uint32_t *out_offset,
                                   struct fat32_dirent **out_entry) {
    if (fs == NULL || dir_start_cluster < 2U || out_cluster == NULL ||
        out_offset == NULL || out_entry == NULL) return false;
    uint32_t cluster = dir_start_cluster;
    uint32_t last_cluster = dir_start_cluster;
    uint32_t steps = 0U;
    while (cluster >= 2U && cluster < 0x0ffffff8U && steps++ < fs->cluster_count) {
        last_cluster = cluster;
        uint8_t *data = fat32_get_cluster_ptr(fs, cluster);
        if (data == NULL) return false;
        uint32_t count = fs->cluster_size / (uint32_t)sizeof(struct fat32_dirent);
        struct fat32_dirent *entries = (struct fat32_dirent *)(uintptr_t)data;
        for (uint32_t i = 0; i < count; i++) {
            uint8_t first_byte = (uint8_t)entries[i].name[0];
            if (first_byte == 0x00U || first_byte == 0xe5U) {
                *out_cluster = cluster;
                *out_offset = (uint32_t)(i * sizeof(struct fat32_dirent));
                *out_entry = &entries[i];
                return true;
            }
        }
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    /* Directory full: extend by 1 cluster */
    uint32_t new_cluster = fat32_extend_chain(fs, last_cluster);
    if (new_cluster == 0U) return false;
    uint8_t *data = fat32_get_cluster_ptr(fs, new_cluster);
    if (data == NULL) return false;
    struct fat32_dirent *entries = (struct fat32_dirent *)(uintptr_t)data;
    *out_cluster = new_cluster;
    *out_offset = 0U;
    *out_entry = &entries[0];
    return true;
}

static int64_t fat32_read(struct vfs_node *node, uint64_t offset, size_t size, void *buffer) {
    if (node == NULL || node->type != VFS_TYPE_FILE || node->device_or_fs_data == NULL || (size != 0U && buffer == NULL))
        return -1;
    if (size == 0U || offset >= node->size) return 0;
    struct fat32_file_info *fi = (struct fat32_file_info *)node->device_or_fs_data;
    struct fat32_fs *fs = &global_fat32_state;
    size_t to_read = size;
    if ((uint64_t)to_read > node->size - offset) to_read = (size_t)(node->size - offset);
    if (fi->first_cluster < 2U || fi->first_cluster > fs->cluster_count + 1U) return -1;
    uint32_t cluster = fi->first_cluster, steps = 0U;
    uint64_t skip = offset;
    while (skip >= fs->cluster_size) {
        if (steps++ >= fs->cluster_count) return -1;
        const uint32_t next = fat32_get_fat_entry(fs, cluster);
        if (next >= 0x0ffffff8U || next < 2U || next > fs->cluster_count + 1U) return -1;
        cluster = next;
        skip -= fs->cluster_size;
    }
    size_t copied = 0U;
    while (copied < to_read && steps < fs->cluster_count) {
        uint8_t *cluster_data = fat32_get_cluster_ptr(fs, cluster);
        if (cluster_data == NULL) break;
        size_t chunk = fs->cluster_size - (size_t)skip;
        if (chunk > to_read - copied) chunk = to_read - copied;
        for (size_t i = 0U; i < chunk; i++) ((uint8_t *)buffer)[copied + i] = cluster_data[(size_t)skip + i];
        copied += chunk;
        skip = 0U;
        ++steps;
        if (copied < to_read) {
            const uint32_t next = fat32_get_fat_entry(fs, cluster);
            if (next >= 0x0ffffff8U || next < 2U || next > fs->cluster_count + 1U) break;
            cluster = next;
        }
    }
    return (int64_t)copied;
}

static int64_t fat32_write(struct vfs_node *node, uint64_t offset, size_t size, const void *buffer) {
    if (node == NULL || node->type != VFS_TYPE_FILE || node->device_or_fs_data == NULL ||
        (size != 0U && buffer == NULL)) return -1;
    if (size == 0U) return 0;
    if (offset > 0xFFFFFFFFULL || size > 0xFFFFFFFFULL - offset) return -1;

    struct fat32_fs *fs = &global_fat32_state;
    struct fat32_file_info *fi = (struct fat32_file_info *)node->device_or_fs_data;

    if (fi->first_cluster < 2U) {
        uint32_t c = fat32_alloc_free_cluster(fs);
        if (c == 0U) return -1;
        fi->first_cluster = c;
        node->inode = c;
        (void)fat32_update_dirent_meta(fs, fi, c, (uint32_t)node->size);
    }

    uint32_t cluster = fi->first_cluster;
    uint64_t skip = offset;
    uint32_t steps = 0U;

    while (skip >= fs->cluster_size) {
        if (steps++ >= fs->cluster_count) return -1;
        uint32_t next = fat32_get_fat_entry(fs, cluster);
        if (next >= 0x0ffffff8U || next < 2U || next > fs->cluster_count + 1U) {
            next = fat32_extend_chain(fs, cluster);
            if (next == 0U) return -1;
        }
        cluster = next;
        skip -= fs->cluster_size;
    }

    size_t written = 0U;
    const uint8_t *src = (const uint8_t *)buffer;

    while (written < size && steps < fs->cluster_count) {
        uint8_t *cluster_data = fat32_get_cluster_ptr(fs, cluster);
        if (cluster_data == NULL) break;

        size_t chunk = fs->cluster_size - (size_t)skip;
        if (chunk > size - written) chunk = size - written;

        for (size_t i = 0U; i < chunk; i++) {
            cluster_data[(size_t)skip + i] = src[written + i];
        }

        written += chunk;
        skip = 0U;
        steps++;

        if (written < size) {
            uint32_t next = fat32_get_fat_entry(fs, cluster);
            if (next >= 0x0ffffff8U || next < 2U || next > fs->cluster_count + 1U) {
                next = fat32_extend_chain(fs, cluster);
                if (next == 0U) break;
            }
            cluster = next;
        }
    }

    if (offset + written > node->size) {
        node->size = offset + written;
        (void)fat32_update_dirent_meta(fs, fi, fi->first_cluster, (uint32_t)node->size);
    }

    return (int64_t)written;
}

static int fat32_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent) {
    if (node == NULL || out_dirent == NULL) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    uint32_t cluster = 0U;
    uint32_t current_idx = 0U, steps = 0U;
    if (node->type != VFS_TYPE_DIRECTORY ||
        (node != &fat32_root_node && node->device_or_fs_data == NULL)) return -1;
    cluster = (node == &fat32_root_node) ? fs->root_cluster :
        ((struct fat32_file_info *)node->device_or_fs_data)->first_cluster;
    while (cluster >= 2U && cluster < 0x0ffffff8U && steps++ < fs->cluster_count) {
        uint8_t *data = fat32_get_cluster_ptr(fs, cluster);
        if (data == NULL) break;
        uint32_t entries = fs->cluster_size / (uint32_t)sizeof(struct fat32_dirent);
        struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)data;

        for (uint32_t i = 0; i < entries; i++) {
            if ((uint8_t)d[i].name[0] == 0x00U) return -1;
            if ((uint8_t)d[i].name[0] == 0xe5U) continue;
            if (d[i].attributes == FAT32_ATTR_LFN ||
                (d[i].attributes & FAT32_ATTR_VOLUME_ID) != 0U ||
                d[i].name[0] == '.') continue;

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
    uint32_t cluster = 0U;
    uint32_t steps = 0U;
    if (node->type != VFS_TYPE_DIRECTORY ||
        (node != &fat32_root_node && node->device_or_fs_data == NULL)) return NULL;
    cluster = (node == &fat32_root_node) ? fs->root_cluster :
        ((struct fat32_file_info *)node->device_or_fs_data)->first_cluster;
    while (cluster >= 2U && cluster < 0x0ffffff8U && steps++ < fs->cluster_count) {
        uint8_t *data = fat32_get_cluster_ptr(fs, cluster);
        if (data == NULL) break;
        uint32_t entries = fs->cluster_size / (uint32_t)sizeof(struct fat32_dirent);
        struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)data;

        for (uint32_t i = 0; i < entries; i++) {
            if ((uint8_t)d[i].name[0] == 0x00U) return NULL;
            if ((uint8_t)d[i].name[0] == 0xe5U) continue;
            if (d[i].attributes == FAT32_ATTR_LFN ||
                (d[i].attributes & FAT32_ATTR_VOLUME_ID) != 0U ||
                d[i].name[0] == '.') continue;

            char formatted[16];
            fat32_format_short_name(d[i].name, formatted);
            if (fat_str_equal(formatted, name)) {
                struct fat32_file_info *fi = NULL;
                struct vfs_node *n = fat32_alloc_node(&fi);
                if (fi == NULL) return NULL;
                fi->first_cluster = ((uint32_t)d[i].first_cluster_high << 16) | d[i].first_cluster_low;
                fi->dir_cluster = cluster;
                fi->dir_offset = (uint32_t)(i * sizeof(struct fat32_dirent));
                fat_str_copy(n->name, formatted, VFS_MAX_NAME);
                n->inode = fi->first_cluster;
                n->type = (d[i].attributes & FAT32_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
                n->size = d[i].file_size;
                n->permissions = (d[i].attributes & FAT32_ATTR_READ_ONLY) ? VFS_FLAG_READ : (VFS_FLAG_READ | VFS_FLAG_WRITE);
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

static int fat32_create(struct vfs_node *parent, const char *name, uint32_t flags, struct vfs_node **out_node) {
    (void)flags;
    if (parent == NULL || name == NULL || parent->type != VFS_TYPE_DIRECTORY) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    char short_name[11];
    if (!fat32_to_short_name(name, short_name)) return -1;
    if (fat32_finddir(parent, name) != NULL) return -1;

    uint32_t dir_cluster = (parent == &fat32_root_node) ? fs->root_cluster :
        ((struct fat32_file_info *)parent->device_or_fs_data)->first_cluster;

    uint32_t target_cluster = 0U;
    uint32_t target_offset = 0U;
    struct fat32_dirent *d = NULL;
    if (!fat32_find_free_dirent(fs, dir_cluster, &target_cluster, &target_offset, &d)) return -1;

    uint32_t first_cluster = fat32_alloc_free_cluster(fs);
    if (first_cluster == 0U) return -1;

    for (int i = 0; i < 11; i++) d->name[i] = short_name[i];
    d->attributes = FAT32_ATTR_ARCHIVE;
    d->reserved_nt = 0;
    d->creation_time_tenth = 0;
    d->creation_time = 0;
    d->creation_date = 0x5501;
    d->last_access_date = 0x5501;
    d->first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFFU);
    d->write_time = 0;
    d->write_date = 0x5501;
    d->first_cluster_low = (uint16_t)(first_cluster & 0xFFFFU);
    d->file_size = 0U;

    struct fat32_file_info *fi = NULL;
    struct vfs_node *n = fat32_alloc_node(&fi);
    if (fi == NULL) return -1;
    fi->first_cluster = first_cluster;
    fi->dir_cluster = target_cluster;
    fi->dir_offset = target_offset;
    fat_str_copy(n->name, name, VFS_MAX_NAME);
    n->inode = first_cluster;
    n->type = VFS_TYPE_FILE;
    n->size = 0U;
    n->permissions = VFS_FLAG_READ | VFS_FLAG_WRITE;
    n->device_or_fs_data = fi;
    n->ops = parent->ops;
    n->parent = parent;
    if (out_node != NULL) *out_node = n;
    return 0;
}

static int fat32_mkdir(struct vfs_node *parent, const char *name, uint32_t flags, struct vfs_node **out_node) {
    (void)flags;
    if (parent == NULL || name == NULL || parent->type != VFS_TYPE_DIRECTORY) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    char short_name[11];
    if (!fat32_to_short_name(name, short_name)) return -1;
    if (fat32_finddir(parent, name) != NULL) return -1;

    uint32_t parent_cluster = (parent == &fat32_root_node) ? fs->root_cluster :
        ((struct fat32_file_info *)parent->device_or_fs_data)->first_cluster;

    uint32_t target_cluster = 0U;
    uint32_t target_offset = 0U;
    struct fat32_dirent *d = NULL;
    if (!fat32_find_free_dirent(fs, parent_cluster, &target_cluster, &target_offset, &d)) return -1;

    uint32_t new_dir_cluster = fat32_alloc_free_cluster(fs);
    if (new_dir_cluster == 0U) return -1;

    uint8_t *dir_data = fat32_get_cluster_ptr(fs, new_dir_cluster);
    if (dir_data == NULL) {
        fat32_free_chain(fs, new_dir_cluster);
        return -1;
    }
    struct fat32_dirent *sub_entries = (struct fat32_dirent *)(uintptr_t)dir_data;
    /* "." entry */
    for (int i = 0; i < 11; i++) sub_entries[0].name[i] = ' ';
    sub_entries[0].name[0] = '.';
    sub_entries[0].attributes = FAT32_ATTR_DIRECTORY;
    sub_entries[0].first_cluster_low = (uint16_t)(new_dir_cluster & 0xFFFFU);
    sub_entries[0].first_cluster_high = (uint16_t)((new_dir_cluster >> 16) & 0xFFFFU);
    sub_entries[0].file_size = 0U;

    /* ".." entry */
    for (int i = 0; i < 11; i++) sub_entries[1].name[i] = ' ';
    sub_entries[1].name[0] = '.';
    sub_entries[1].name[1] = '.';
    sub_entries[1].attributes = FAT32_ATTR_DIRECTORY;
    uint32_t dotdot_cluster = (parent == &fat32_root_node) ? 0U : parent_cluster;
    sub_entries[1].first_cluster_low = (uint16_t)(dotdot_cluster & 0xFFFFU);
    sub_entries[1].first_cluster_high = (uint16_t)((dotdot_cluster >> 16) & 0xFFFFU);
    sub_entries[1].file_size = 0U;

    /* Entry in parent directory */
    for (int i = 0; i < 11; i++) d->name[i] = short_name[i];
    d->attributes = FAT32_ATTR_DIRECTORY;
    d->reserved_nt = 0;
    d->creation_time_tenth = 0;
    d->creation_time = 0;
    d->creation_date = 0x5501;
    d->last_access_date = 0x5501;
    d->first_cluster_high = (uint16_t)((new_dir_cluster >> 16) & 0xFFFFU);
    d->write_time = 0;
    d->write_date = 0x5501;
    d->first_cluster_low = (uint16_t)(new_dir_cluster & 0xFFFFU);
    d->file_size = 0U;

    struct fat32_file_info *fi = NULL;
    struct vfs_node *n = fat32_alloc_node(&fi);
    if (fi == NULL) return -1;
    fi->first_cluster = new_dir_cluster;
    fi->dir_cluster = target_cluster;
    fi->dir_offset = target_offset;
    fat_str_copy(n->name, name, VFS_MAX_NAME);
    n->inode = new_dir_cluster;
    n->type = VFS_TYPE_DIRECTORY;
    n->size = 0U;
    n->permissions = VFS_FLAG_READ | VFS_FLAG_WRITE;
    n->device_or_fs_data = fi;
    n->ops = parent->ops;
    n->parent = parent;
    if (out_node != NULL) *out_node = n;
    return 0;
}

static int fat32_unlink(struct vfs_node *parent, const char *name) {
    if (parent == NULL || name == NULL || parent->type != VFS_TYPE_DIRECTORY) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    struct vfs_node *victim = fat32_finddir(parent, name);
    if (victim == NULL || victim->device_or_fs_data == NULL) return -1;
    struct fat32_file_info *fi = (struct fat32_file_info *)victim->device_or_fs_data;

    if (victim->type == VFS_TYPE_DIRECTORY) {
        uint8_t *dir_data = fat32_get_cluster_ptr(fs, fi->first_cluster);
        if (dir_data == NULL) return -1;
        uint32_t count = fs->cluster_size / (uint32_t)sizeof(struct fat32_dirent);
        struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)dir_data;
        for (uint32_t i = 0; i < count; i++) {
            if ((uint8_t)d[i].name[0] == 0x00U) break;
            if ((uint8_t)d[i].name[0] == 0xe5U) continue;
            if (d[i].name[0] == '.') continue;
            return -1;
        }
    }

    uint8_t *pdata = fat32_get_cluster_ptr(fs, fi->dir_cluster);
    if (pdata == NULL || (size_t)fi->dir_offset + sizeof(struct fat32_dirent) > fs->cluster_size) return -1;
    struct fat32_dirent *d = (struct fat32_dirent *)(uintptr_t)(pdata + fi->dir_offset);
    d->name[0] = (char)0xe5;

    if (fi->first_cluster >= 2U) {
        fat32_free_chain(fs, fi->first_cluster);
    }

    victim->type = VFS_TYPE_INVALID;
    victim->size = 0U;
    victim->device_or_fs_data = NULL;
    victim->inode = 0ULL;
    return 0;
}

int fat32_truncate(struct vfs_node *node, uint64_t new_size) {
    if (node == NULL || node->type != VFS_TYPE_FILE || node->device_or_fs_data == NULL) return -1;
    if (new_size > 0xFFFFFFFFULL) return -1;
    struct fat32_fs *fs = &global_fat32_state;
    struct fat32_file_info *fi = (struct fat32_file_info *)node->device_or_fs_data;
    if (new_size == 0ULL) {
        if (fi->first_cluster >= 2U) {
            fat32_free_chain(fs, fi->first_cluster);
            fi->first_cluster = 0U;
            node->inode = 0ULL;
        }
        node->size = 0ULL;
        (void)fat32_update_dirent_meta(fs, fi, 0U, 0U);
        return 0;
    }
    if (new_size < node->size && fi->first_cluster >= 2U) {
        uint32_t cluster = fi->first_cluster;
        uint64_t offset = 0ULL;
        uint32_t steps = 0U;
        while (offset + fs->cluster_size < new_size && steps++ < fs->cluster_count) {
            uint32_t next = fat32_get_fat_entry(fs, cluster);
            if (next >= 0x0ffffff8U || next < 2U || next > fs->cluster_count + 1U) break;
            cluster = next;
            offset += fs->cluster_size;
        }
        uint32_t to_free = fat32_get_fat_entry(fs, cluster);
        (void)fat32_set_fat_entry(fs, cluster, 0x0FFFFFFFU);
        if (to_free >= 2U && to_free < 0x0ffffff8U) {
            fat32_free_chain(fs, to_free);
        }
    }
    node->size = new_size;
    (void)fat32_update_dirent_meta(fs, fi, fi->first_cluster, (uint32_t)node->size);
    return 0;
}

static const struct vfs_operations fat32_ops = {
    .read = fat32_read,
    .write = fat32_write,
    .open = NULL,
    .close = NULL,
    .readdir = fat32_readdir,
    .finddir = fat32_finddir,
    .create = fat32_create,
    .mkdir = fat32_mkdir,
    .unlink = fat32_unlink
};

struct vfs_node *fat32_mount_image(void *image_data, size_t image_size) {
    if (image_data == NULL || image_size < 512U) return NULL;
    const uint8_t *boot = (const uint8_t *)image_data;
    if (boot[510] != 0x55U || boot[511] != 0xaaU) return NULL;
    struct fat32_bpb *bpb = (struct fat32_bpb *)image_data;
    const uint32_t bps = bpb->bytes_per_sector, spc = bpb->sectors_per_cluster;
    const uint32_t total = bpb->total_sectors_16 != 0U ? bpb->total_sectors_16 : bpb->total_sectors_32;
    if ((bps != 512U && bps != 1024U && bps != 2048U && bps != 4096U) ||
        spc == 0U || spc > 128U || (spc & (spc - 1U)) != 0U ||
        bpb->reserved_sector_count == 0U || bpb->table_count == 0U || bpb->table_count > 4U ||
        bpb->table_size_32 == 0U || total == 0U || bpb->root_cluster < 2U) return NULL;
    const uint64_t data_start = (uint64_t)bpb->reserved_sector_count + (uint64_t)bpb->table_count * bpb->table_size_32;
    if (data_start >= total || data_start > UINT32_MAX) return NULL;
    const uint64_t total_bytes = (uint64_t)total * bps;
    if (total_bytes > image_size) return NULL;
    const uint64_t clusters = ((uint64_t)total - data_start) / spc;
    if (clusters == 0ULL || clusters > UINT32_MAX - 1ULL || bpb->root_cluster > clusters + 1ULL) return NULL;
    const uint64_t fat_entries = ((uint64_t)bpb->table_size_32 * bps) / 4ULL;
    if (fat_entries < clusters + 2ULL) return NULL;
    const uint64_t cluster_size = (uint64_t)bps * spc;
    if (cluster_size == 0ULL || cluster_size > 65536ULL) return NULL;
    global_fat32_state = (struct fat32_fs){
        .image = (uint8_t *)image_data, .size = image_size, .bytes_per_sector = bps,
        .sectors_per_cluster = spc, .cluster_size = (uint32_t)cluster_size,
        .reserved_sectors = bpb->reserved_sector_count, .fat_count = bpb->table_count,
        .fat_size_sectors = bpb->table_size_32, .root_cluster = bpb->root_cluster,
        .data_start_sector = (uint32_t)data_start, .total_sectors = total,
        .cluster_count = (uint32_t)clusters
    };
    fat32_node_count = 0U;
    if (fat32_get_cluster_ptr(&global_fat32_state, bpb->root_cluster) == NULL) return NULL;
    fat32_root_node = (struct vfs_node){0};
    fat_str_copy(fat32_root_node.name, "fat32", VFS_MAX_NAME);
    fat32_root_node.type = VFS_TYPE_DIRECTORY;
    fat32_root_node.permissions = VFS_FLAG_READ | VFS_FLAG_WRITE;
    fat32_root_node.ops = fat32_ops;
    fat32_root_node.inode = bpb->root_cluster;
    return &fat32_root_node;
}

bool fat32_mount(struct block_device *bdev, struct vfs_node **out_root) {
    if (bdev == NULL || out_root == NULL || bdev->driver_data == NULL || bdev->capacity_bytes < 512)
        return false;
    struct vfs_node *root = fat32_mount_image(bdev->driver_data, (size_t)bdev->capacity_bytes);
    if (root == NULL) return false;
    *out_root = root;
    return true;
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
    image[510] = 0x55U; image[511] = 0xaaU;

    /* Initialize FAT entries for root cluster (2) and initial file cluster (3) across FAT0 and FAT1 */
    uint32_t *fat0 = (uint32_t *)(uintptr_t)(image + (32 * 512));
    uint32_t *fat1 = (uint32_t *)(uintptr_t)(image + ((32 + 8) * 512));
    fat0[0] = 0x0ffffff8U; fat1[0] = 0x0ffffff8U;
    fat0[1] = 0x0fffffffU; fat1[1] = 0x0fffffffU;
    fat0[2] = 0x0ffffff8U; fat1[2] = 0x0ffffff8U; /* Root cluster EOC */
    fat0[3] = 0x0ffffff8U; fat1[3] = 0x0ffffff8U; /* File cluster EOC */

    /* Put initial TEST.TXT entry in root cluster (cluster 2 -> data sector 0) */
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

    /* 1. Read existing file */
    struct vfs_node *file_node = fat32_finddir(root, "TEST.TXT");
    if (file_node == NULL || file_node->size != 14) return false;
    char buf[16] = {0};
    if (fat32_read(file_node, 0, 14, buf) != 14) return false;
    for (int i = 0; i < 14; i++) {
        if (buf[i] != test_content[i]) return false;
    }

    /* 2. Create a new file NEW.TXT */
    struct vfs_node *new_node = NULL;
    if (fat32_create(root, "NEW.TXT", VFS_FLAG_READ | VFS_FLAG_WRITE, &new_node) != 0 || new_node == NULL)
        return false;

    /* 3. Write data to NEW.TXT spanning across cluster boundary (> 512 bytes) */
    uint8_t write_pattern[768];
    for (size_t i = 0; i < sizeof(write_pattern); i++) {
        write_pattern[i] = (uint8_t)(i & 0xFFU);
    }
    if (fat32_write(new_node, 0, sizeof(write_pattern), write_pattern) != (int64_t)sizeof(write_pattern))
        return false;
    if (new_node->size != sizeof(write_pattern)) return false;

    /* 4. Verify FAT mirroring (both FAT0 and FAT1 must reflect the new clusters) */
    uint32_t new_fc = (uint32_t)new_node->inode;
    if (new_fc < 2U || fat0[new_fc] >= 0x0ffffff8U) return false;
    if (fat0[new_fc] != fat1[new_fc]) return false;

    /* 5. Read back multi-cluster data and verify */
    uint8_t read_pattern[768] = {0};
    if (fat32_read(new_node, 0, sizeof(read_pattern), read_pattern) != (int64_t)sizeof(read_pattern))
        return false;
    for (size_t i = 0; i < sizeof(write_pattern); i++) {
        if (read_pattern[i] != write_pattern[i]) return false;
    }

    /* 6. Create subdirectory DOCS */
    struct vfs_node *docs_dir = NULL;
    if (fat32_mkdir(root, "DOCS", 0755, &docs_dir) != 0 || docs_dir == NULL)
        return false;

    /* 7. Create and write file inside DOCS directory */
    struct vfs_node *sub_file = NULL;
    if (fat32_create(docs_dir, "SUB.TXT", VFS_FLAG_READ | VFS_FLAG_WRITE, &sub_file) != 0 || sub_file == NULL)
        return false;
    static const char sub_data[] = "Nested FAT32 File Content";
    if (fat32_write(sub_file, 0, sizeof(sub_data), sub_data) != (int64_t)sizeof(sub_data))
        return false;

    /* 8. Verify directory readdir */
    struct vfs_dirent dent = {0};
    if (fat32_readdir(root, 0, &dent) != 0) return false;

    /* 9. Attempt to unlink non-empty directory -> must fail */
    if (fat32_unlink(root, "DOCS") == 0) return false;

    /* 10. Unlink subfile then unlink empty directory -> must succeed */
    if (fat32_unlink(docs_dir, "SUB.TXT") != 0) return false;
    if (fat32_unlink(root, "DOCS") != 0) return false;

    /* 11. Unlink NEW.TXT and verify clusters freed in both FATs */
    if (fat32_unlink(root, "NEW.TXT") != 0) return false;
    if (fat0[new_fc] != 0x00000000U || fat1[new_fc] != 0x00000000U) return false;

    /* 12. Malformed BPB rejection test matrix */
    if (fat32_mount_image(NULL, sizeof(image)) != NULL) return false;
    if (fat32_mount_image(image, 256) != NULL) return false;

    image[510] = 0x00U;
    if (fat32_mount_image(image, sizeof(image)) != NULL) return false;
    image[510] = 0x55U;

    bpb->bytes_per_sector = 300;
    if (fat32_mount_image(image, sizeof(image)) != NULL) return false;
    bpb->bytes_per_sector = 512;

    bpb->sectors_per_cluster = 0;
    if (fat32_mount_image(image, sizeof(image)) != NULL) return false;
    bpb->sectors_per_cluster = 1;

    bpb->root_cluster = 1;
    if (fat32_mount_image(image, sizeof(image)) != NULL) return false;
    bpb->root_cluster = 2;

    return true;
}
#endif
