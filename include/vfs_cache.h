/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_VFS_CACHE_H
#define DAWN_VFS_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define VFS_CACHE_MAX_ENTRIES 32U

struct vfs_cache_entry {
    char path[VFS_MAX_PATH];
    struct vfs_node *node;
    uint64_t access_time;
    bool valid;
};

void vfs_cache_initialize(void);
struct vfs_node *vfs_cache_lookup(const char *path);
void vfs_cache_insert(const char *path, struct vfs_node *node);
void vfs_cache_invalidate(const char *path);

#ifdef DAWN_KERNEL_TEST
bool vfs_cache_self_test(void);
#endif

#endif
