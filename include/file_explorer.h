/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_FILE_EXPLORER_H
#define DAWN_FILE_EXPLORER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define EXPLORER_MAX_ITEMS 64U

enum explorer_item_type {
    EXPLORER_ITEM_UNKNOWN = 0,
    EXPLORER_ITEM_FILE,
    EXPLORER_ITEM_DIRECTORY,
    EXPLORER_ITEM_DEVICE
};

struct explorer_file_item {
    char name[VFS_MAX_NAME];
    char path[VFS_MAX_PATH];
    uint64_t size;
    enum explorer_item_type type;
    uint32_t permissions;
};

struct explorer_listing {
    char current_path[VFS_MAX_PATH];
    struct explorer_file_item items[EXPLORER_MAX_ITEMS];
    uint32_t count;
    uint64_t total_size;
};

/* File explorer backend APIs */
bool explorer_list_directory(const char *path, struct explorer_listing *out_listing);
bool explorer_search(const char *root_path, const char *query, struct explorer_listing *out_results);
bool explorer_copy_file(const char *src_path, const char *dst_path);
bool explorer_move_file(const char *src_path, const char *dst_path);
bool explorer_delete_file(const char *path);

#ifdef DAWN_KERNEL_TEST
bool explorer_self_test(void);
#endif

#endif
