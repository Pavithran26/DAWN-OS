/* SPDX-License-Identifier: Unlicense */
#include "file_explorer.h"

static inline void exp_str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool exp_str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static inline bool exp_str_contains(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) return false;
    if (needle[0] == '\0') return true;
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (haystack[i + j] != '\0' && needle[j] != '\0' && haystack[i + j] == needle[j]) {
            j++;
        }
        if (needle[j] == '\0') return true;
    }
    return false;
}

bool explorer_list_directory(const char *path, struct explorer_listing *out_listing) {
    if (path == NULL || out_listing == NULL) return false;
    struct vfs_node *dir_node = NULL;
    if (!vfs_resolve_path(path, &dir_node) || dir_node == NULL || dir_node->type != VFS_TYPE_DIRECTORY)
        return false;

    *out_listing = (struct explorer_listing){0};
    exp_str_copy(out_listing->current_path, path, VFS_MAX_PATH);

    for (uint32_t i = 0; i < EXPLORER_MAX_ITEMS; i++) {
        struct vfs_dirent entry = {0};
        if (vfs_readdir(dir_node, i, &entry) != 0) break;
        struct explorer_file_item *item = &out_listing->items[out_listing->count++];
        exp_str_copy(item->name, entry.name, VFS_MAX_NAME);
        item->size = entry.size;
        item->type = (entry.type == VFS_TYPE_DIRECTORY) ? EXPLORER_ITEM_DIRECTORY : EXPLORER_ITEM_FILE;

        /* Build full path */
        size_t plen = 0;
        while (path[plen] != '\0' && plen + 1 < VFS_MAX_PATH) {
            item->path[plen] = path[plen];
            plen++;
        }
        if (plen > 0 && item->path[plen - 1] != '/' && plen + 1 < VFS_MAX_PATH) {
            item->path[plen++] = '/';
        }
        size_t nlen = 0;
        while (entry.name[nlen] != '\0' && plen + 1 < VFS_MAX_PATH) {
            item->path[plen++] = entry.name[nlen++];
        }
        item->path[plen] = '\0';
        out_listing->total_size += entry.size;
    }
    return true;
}

static void search_recursive(struct vfs_node *node, const char *base_path,
                             const char *query, struct explorer_listing *out_results,
                             int depth) {
    if (node == NULL || depth > 8 || out_results->count >= EXPLORER_MAX_ITEMS) return;
    for (uint32_t i = 0; i < EXPLORER_MAX_ITEMS; i++) {
        struct vfs_dirent entry = {0};
        if (vfs_readdir(node, i, &entry) != 0) break;

        char sub_path[VFS_MAX_PATH];
        size_t plen = 0;
        while (base_path[plen] != '\0' && plen + 1 < VFS_MAX_PATH) {
            sub_path[plen] = base_path[plen];
            plen++;
        }
        if (plen > 0 && sub_path[plen - 1] != '/' && plen + 1 < VFS_MAX_PATH) {
            sub_path[plen++] = '/';
        }
        size_t nlen = 0;
        while (entry.name[nlen] != '\0' && plen + 1 < VFS_MAX_PATH) {
            sub_path[plen++] = entry.name[nlen++];
        }
        sub_path[plen] = '\0';

        if (exp_str_contains(entry.name, query) && out_results->count < EXPLORER_MAX_ITEMS) {
            struct explorer_file_item *item = &out_results->items[out_results->count++];
            exp_str_copy(item->name, entry.name, VFS_MAX_NAME);
            exp_str_copy(item->path, sub_path, VFS_MAX_PATH);
            item->size = entry.size;
            item->type = (entry.type == VFS_TYPE_DIRECTORY) ? EXPLORER_ITEM_DIRECTORY : EXPLORER_ITEM_FILE;
        }

        if (entry.type == VFS_TYPE_DIRECTORY) {
            struct vfs_node *sub_node = vfs_finddir(node, entry.name);
            if (sub_node != NULL) {
                search_recursive(sub_node, sub_path, query, out_results, depth + 1);
            }
        }
    }
}

bool explorer_search(const char *root_path, const char *query, struct explorer_listing *out_results) {
    if (root_path == NULL || query == NULL || out_results == NULL) return false;
    struct vfs_node *root = NULL;
    if (!vfs_resolve_path(root_path, &root) || root == NULL) return false;

    *out_results = (struct explorer_listing){0};
    exp_str_copy(out_results->current_path, root_path, VFS_MAX_PATH);
    search_recursive(root, root_path, query, out_results, 0);
    return true;
}

bool explorer_copy_file(const char *src_path, const char *dst_path) {
    if (src_path == NULL || dst_path == NULL) return false;
    struct vfs_node *src_node = NULL;
    if (!vfs_resolve_path(src_path, &src_node) || src_node == NULL || src_node->type != VFS_TYPE_FILE)
        return false;

    struct vfs_node *dst_node = NULL;
    if (vfs_create(dst_path, VFS_FLAG_READ | VFS_FLAG_WRITE, &dst_node) != 0 || dst_node == NULL)
        return false;

    uint8_t buffer[512];
    uint64_t offset = 0;
    while (offset < src_node->size) {
        size_t chunk = sizeof(buffer);
        if (offset + chunk > src_node->size) chunk = (size_t)(src_node->size - offset);
        int64_t bytes_read = vfs_read(src_node, offset, chunk, buffer);
        if (bytes_read <= 0) return false;
        int64_t bytes_written = vfs_write(dst_node, offset, (size_t)bytes_read, buffer);
        if (bytes_written != bytes_read) return false;
        offset += (uint64_t)bytes_read;
    }
    return offset == src_node->size;
}

bool explorer_move_file(const char *src_path, const char *dst_path) {
    if (!explorer_copy_file(src_path, dst_path)) return false;
    return explorer_delete_file(src_path);
}

bool explorer_delete_file(const char *path) {
    return vfs_unlink(path) == 0;
}

#ifdef DAWN_KERNEL_TEST
bool explorer_self_test(void) {
    struct explorer_listing listing = {0};
    if (!explorer_list_directory("/", &listing)) return false;
    if (listing.count < 3) return false; /* boot, dev, tmp etc */

    /* Create test file */
    struct vfs_node *created = NULL;
    if (vfs_create("/tmp/explorer_test.txt", VFS_FLAG_READ | VFS_FLAG_WRITE, &created) != 0)
        return false;
    static const char sample_data[] = "Emerald Green Explorer Test Content";
    vfs_write(created, 0, sizeof(sample_data), sample_data);

    /* Search for explorer_test */
    struct explorer_listing search_res = {0};
    if (!explorer_search("/", "explorer_test", &search_res) || search_res.count == 0)
        return false;

    /* Copy file */
    if (!explorer_copy_file("/tmp/explorer_test.txt", "/tmp/copied.txt"))
        return false;

    struct vfs_node *copied = NULL;
    if (!vfs_resolve_path("/tmp/copied.txt", &copied) || copied == NULL || copied->size != sizeof(sample_data))
        return false;

    /* Clean up */
    explorer_delete_file("/tmp/explorer_test.txt");
    explorer_delete_file("/tmp/copied.txt");
    return true;
}
#endif
