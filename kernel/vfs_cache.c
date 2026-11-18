/* SPDX-License-Identifier: Unlicense */
#include "vfs_cache.h"

static struct vfs_cache_entry cache_table[VFS_CACHE_MAX_ENTRIES];
static uint64_t cache_clock;
static bool initialized;

static inline void c_str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool c_str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

void vfs_cache_initialize(void) {
    for (size_t i = 0; i < VFS_CACHE_MAX_ENTRIES; i++) {
        cache_table[i] = (struct vfs_cache_entry){0};
    }
    cache_clock = 1;
    initialized = true;
}

struct vfs_node *vfs_cache_lookup(const char *path) {
    if (!initialized || path == NULL) return NULL;
    for (size_t i = 0; i < VFS_CACHE_MAX_ENTRIES; i++) {
        if (cache_table[i].valid && c_str_equal(cache_table[i].path, path)) {
            cache_table[i].access_time = ++cache_clock;
            return cache_table[i].node;
        }
    }
    return NULL;
}

void vfs_cache_insert(const char *path, struct vfs_node *node) {
    if (!initialized || path == NULL || node == NULL) return;
    for (size_t i = 0; i < VFS_CACHE_MAX_ENTRIES; i++) {
        if (cache_table[i].valid && c_str_equal(cache_table[i].path, path)) {
            cache_table[i].node = node;
            cache_table[i].access_time = ++cache_clock;
            return;
        }
    }
    /* Find empty or LRU slot */
    size_t lru_idx = 0;
    uint64_t oldest_time = UINT64_MAX;
    for (size_t i = 0; i < VFS_CACHE_MAX_ENTRIES; i++) {
        if (!cache_table[i].valid) {
            lru_idx = i;
            break;
        }
        if (cache_table[i].access_time < oldest_time) {
            oldest_time = cache_table[i].access_time;
            lru_idx = i;
        }
    }
    cache_table[lru_idx].valid = true;
    c_str_copy(cache_table[lru_idx].path, path, VFS_MAX_PATH);
    cache_table[lru_idx].node = node;
    cache_table[lru_idx].access_time = ++cache_clock;
}

void vfs_cache_invalidate(const char *path) {
    if (!initialized || path == NULL) return;
    for (size_t i = 0; i < VFS_CACHE_MAX_ENTRIES; i++) {
        if (cache_table[i].valid && c_str_equal(cache_table[i].path, path)) {
            cache_table[i].valid = false;
        }
    }
}

#ifdef DAWN_KERNEL_TEST
bool vfs_cache_self_test(void) {
    vfs_cache_initialize();
    struct vfs_node dummy_node = { .inode = 42 };

    if (vfs_cache_lookup("/test/path") != NULL) return false;
    vfs_cache_insert("/test/path", &dummy_node);
    if (vfs_cache_lookup("/test/path") != &dummy_node) return false;

    vfs_cache_invalidate("/test/path");
    if (vfs_cache_lookup("/test/path") != NULL) return false;

    return true;
}
#endif
