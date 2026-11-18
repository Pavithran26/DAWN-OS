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

static int64_t iso9660_read(struct vfs_node *node, uint64_t offset, size_t size, void *buffer) {
    if (node == NULL || buffer == NULL || node->device_or_fs_data == NULL) return -1;
    struct iso9660_file_info *fi = (struct iso9660_file_info *)node->device_or_fs_data;
    struct iso9660_fs *fs = &global_iso_state;
    if (offset >= node->size) return 0;
    size_t to_read = size;
    if (offset + to_read > node->size) to_read = (size_t)(node->size - offset);

    uint64_t start_pos = (uint64_t)fi->extent_lba * fs->block_size + offset;
    if (start_pos + to_read > fs->size) {
        if (start_pos >= fs->size) return 0;
        to_read = (size_t)(fs->size - start_pos);
    }

    uint8_t *src = fs->image + start_pos;
    uint8_t *dst = (uint8_t *)buffer;
    for (size_t i = 0; i < to_read; i++) dst[i] = src[i];
    return (int64_t)to_read;
}

static int iso9660_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent) {
    if (node == NULL || out_dirent == NULL) return -1;
    struct iso9660_fs *fs = &global_iso_state;
    uint32_t extent = (node == &iso_root_node) ? fs->root_extent :
        ((struct iso9660_file_info *)node->device_or_fs_data)->extent_lba;
    uint64_t dir_size = node->size;

    uint64_t offset = 0;
    uint32_t curr_idx = 0;
    uint8_t *data = fs->image + (uint64_t)extent * fs->block_size;

    while (offset < dir_size) {
        struct iso9660_dirent *d = (struct iso9660_dirent *)(uintptr_t)(data + offset);
        if (d->length == 0) break;

        /* Skip root and parent entries (name_len == 1 and name[0] == 0 or 1) */
        if (d->name_len > 1 || (d->name[0] != 0 && d->name[0] != 1)) {
            if (curr_idx == index) {
                iso_format_name(d->name, d->name_len, out_dirent->name, sizeof(out_dirent->name));
                out_dirent->inode = d->extent_lba_le;
                out_dirent->type = (d->file_flags & ISO9660_FLAG_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
                out_dirent->size = d->data_length_le;
                return 0;
            }
            curr_idx++;
        }
        offset += d->length;
    }
    return -1;
}

static struct vfs_node *iso9660_finddir(struct vfs_node *node, const char *name) {
    if (node == NULL || name == NULL) return NULL;
    struct iso9660_fs *fs = &global_iso_state;
    uint32_t extent = (node == &iso_root_node) ? fs->root_extent :
        ((struct iso9660_file_info *)node->device_or_fs_data)->extent_lba;
    uint64_t dir_size = node->size;

    uint64_t offset = 0;
    uint8_t *data = fs->image + (uint64_t)extent * fs->block_size;

    while (offset < dir_size) {
        struct iso9660_dirent *d = (struct iso9660_dirent *)(uintptr_t)(data + offset);
        if (d->length == 0) break;

        if (d->name_len > 1 || (d->name[0] != 0 && d->name[0] != 1)) {
            char formatted[VFS_MAX_NAME];
            iso_format_name(d->name, d->name_len, formatted, sizeof(formatted));
            if (iso_str_equal(formatted, name)) {
                if (iso_node_count >= 16) return NULL;
                struct vfs_node *n = &iso_file_nodes[iso_node_count++];
                static struct iso9660_file_info fi_pool[16];
                struct iso9660_file_info *fi = &fi_pool[iso_node_count - 1];
                fi->extent_lba = d->extent_lba_le;
                *n = (struct vfs_node){0};
                iso_str_copy(n->name, formatted, VFS_MAX_NAME);
                n->inode = d->extent_lba_le;
                n->type = (d->file_flags & ISO9660_FLAG_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
                n->size = d->data_length_le;
                n->permissions = VFS_FLAG_READ;
                n->device_or_fs_data = fi;
                n->ops = node->ops;
                n->parent = node;
                return n;
            }
        }
        offset += d->length;
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

struct vfs_node *iso9660_mount_image(void *image_data, size_t image_size) {
    if (image_data == NULL || image_size < (ISO9660_PVD_SECTOR + 1U) * ISO9660_SECTOR_SIZE)
        return NULL;
    uint8_t *pvd_raw = (uint8_t *)image_data + (ISO9660_PVD_SECTOR * ISO9660_SECTOR_SIZE);
    struct iso9660_pvd *pvd = (struct iso9660_pvd *)(uintptr_t)pvd_raw;

    if (pvd->type != 1 || pvd->id[0] != 'C' || pvd->id[1] != 'D' ||
        pvd->id[2] != '0' || pvd->id[3] != '0' || pvd->id[4] != '1')
        return NULL;

    struct iso9660_dirent *root_dir = (struct iso9660_dirent *)(uintptr_t)pvd->root_directory_record;

    global_iso_state.image = (uint8_t *)image_data;
    global_iso_state.size = image_size;
    global_iso_state.block_size = pvd->logical_block_size_le ? pvd->logical_block_size_le : 2048U;
    global_iso_state.root_extent = root_dir->extent_lba_le;
    global_iso_state.root_size = root_dir->data_length_le;

    iso_node_count = 0;
    iso_root_node = (struct vfs_node){0};
    iso_str_copy(iso_root_node.name, "iso9660", VFS_MAX_NAME);
    iso_root_node.type = VFS_TYPE_DIRECTORY;
    iso_root_node.permissions = VFS_FLAG_READ;
    iso_root_node.size = root_dir->data_length_le;
    iso_root_node.ops = iso9660_ops;
    iso_root_node.inode = root_dir->extent_lba_le;

    return &iso_root_node;
}

#ifdef DAWN_KERNEL_TEST
bool iso9660_self_test(void) {
    /* Create a 64KB synthetic ISO9660 image (32 sectors of 2048 bytes) */
    static uint8_t image[65536] __attribute__((aligned(16)));
    for (size_t i = 0; i < sizeof(image); i++) image[i] = 0;

    /* PVD at sector 16 (offset 32768) */
    struct iso9660_pvd *pvd = (struct iso9660_pvd *)(uintptr_t)(image + 16 * 2048);
    pvd->type = 1;
    pvd->id[0] = 'C'; pvd->id[1] = 'D'; pvd->id[2] = '0'; pvd->id[3] = '0'; pvd->id[4] = '1';
    pvd->logical_block_size_le = 2048;

    /* Root directory record pointing to sector 17 */
    struct iso9660_dirent *root_rec = (struct iso9660_dirent *)(uintptr_t)pvd->root_directory_record;
    root_rec->length = 34;
    root_rec->extent_lba_le = 17;
    root_rec->data_length_le = 2048;
    root_rec->file_flags = ISO9660_FLAG_DIRECTORY;
    root_rec->name_len = 1;
    root_rec->name[0] = 0;

    /* Sector 17 contains directory entries: entry 0 = self, entry 1 = parent, entry 2 = TEST.TXT;1 */
    uint8_t *dir_sector = image + 17 * 2048;

    /* Entry 2: TEST.TXT;1 pointing to sector 18 */
    struct iso9660_dirent *test_ent = (struct iso9660_dirent *)(uintptr_t)(dir_sector + 68);
    test_ent->length = 44;
    test_ent->extent_lba_le = 18;
    test_ent->data_length_le = 15;
    test_ent->file_flags = 0;
    test_ent->name_len = 10;
    static const char name_str[] = "TEST.TXT;1";
    for (int i = 0; i < 10; i++) test_ent->name[i] = name_str[i];

    /* Sector 18: File content */
    uint8_t *file_data = image + 18 * 2048;
    static const char iso_content[] = "ISO9660-OK-DATA";
    for (size_t i = 0; i < 15; i++) file_data[i] = (uint8_t)iso_content[i];

    /* Mount image */
    struct vfs_node *root = iso9660_mount_image(image, sizeof(image));
    if (root == NULL) return false;

    /* Find TEST.TXT */
    struct vfs_node *file_node = iso9660_finddir(root, "TEST.TXT");
    if (file_node == NULL || file_node->size != 15) return false;

    /* Read back file */
    char buf[16] = {0};
    if (iso9660_read(file_node, 0, 15, buf) != 15) return false;
    for (int i = 0; i < 15; i++) {
        if (buf[i] != iso_content[i]) return false;
    }

    return true;
}
#endif
