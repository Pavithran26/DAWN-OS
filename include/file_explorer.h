/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_FILE_EXPLORER_H
#define DAWN_FILE_EXPLORER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define EXPLORER_MAX_ITEMS 64U
#define EXPLORER_WIDTH     560U
#define EXPLORER_HEIGHT    360U

struct window;

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
bool explorer_create_directory(const char *path);
bool explorer_create_file(const char *path);

/* Files GUI Application (Milestone D5) */
struct window *file_explorer_window_open(void);
void file_explorer_render(void);
void file_explorer_handle_click(int32_t local_x, int32_t local_y);
bool file_explorer_navigate(const char *path);
bool file_explorer_navigate_up(void);
const char *file_explorer_get_current_path(void);
int32_t file_explorer_get_selected_index(void);

#ifdef DAWN_KERNEL_TEST
bool explorer_self_test(void);
#endif

#endif
