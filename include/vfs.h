/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_VFS_H
#define DAWN_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_PATH 256U
#define VFS_MAX_NAME 128U
#define VFS_MAX_MOUNTS 8U
#define VFS_MAX_NODES 64U

enum vfs_node_type {
    VFS_TYPE_INVALID = 0,
    VFS_TYPE_FILE,
    VFS_TYPE_DIRECTORY,
    VFS_TYPE_CHARDEVICE,
    VFS_TYPE_BLOCKDEVICE,
    VFS_TYPE_SYMLINK
};

#define VFS_FLAG_READ  0x0001U
#define VFS_FLAG_WRITE 0x0002U
#define VFS_FLAG_EXEC  0x0004U

struct vfs_dirent {
    char name[VFS_MAX_NAME];
    uint64_t inode;
    enum vfs_node_type type;
    uint64_t size;
};

struct vfs_node;

typedef int64_t (*vfs_read_t)(struct vfs_node *node, uint64_t offset, size_t size, void *buffer);
typedef int64_t (*vfs_write_t)(struct vfs_node *node, uint64_t offset, size_t size, const void *buffer);
typedef int (*vfs_open_t)(struct vfs_node *node, uint32_t flags);
typedef int (*vfs_close_t)(struct vfs_node *node);
typedef int (*vfs_readdir_t)(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent);
typedef struct vfs_node *(*vfs_finddir_t)(struct vfs_node *node, const char *name);
typedef int (*vfs_create_t)(struct vfs_node *parent, const char *name, uint32_t flags, struct vfs_node **out_node);
typedef int (*vfs_mkdir_t)(struct vfs_node *parent, const char *name, uint32_t flags, struct vfs_node **out_node);
typedef int (*vfs_unlink_t)(struct vfs_node *parent, const char *name);

struct vfs_operations {
    vfs_read_t read;
    vfs_write_t write;
    vfs_open_t open;
    vfs_close_t close;
    vfs_readdir_t readdir;
    vfs_finddir_t finddir;
    vfs_create_t create;
    vfs_mkdir_t mkdir;
    vfs_unlink_t unlink;
};

struct vfs_node {
    char name[VFS_MAX_NAME];
    uint64_t inode;
    enum vfs_node_type type;
    uint32_t permissions;
    uint64_t size;
    uint64_t ref_count;
    struct vfs_operations ops;
    void *device_or_fs_data;
    struct vfs_node *parent;
    struct vfs_node *mount_point;
};

struct vfs_mount {
    char path[VFS_MAX_PATH];
    struct vfs_node *root_node;
    const char *fs_name;
    bool active;
};

/* VFS API */
bool vfs_initialize(void);
struct vfs_node *vfs_get_root(void);
bool vfs_mount(const char *mount_path, struct vfs_node *fs_root, const char *fs_name);
bool vfs_unmount(const char *mount_path);
bool vfs_resolve_path(const char *path, struct vfs_node **out_node);
int64_t vfs_read(struct vfs_node *node, uint64_t offset, size_t size, void *buffer);
int64_t vfs_write(struct vfs_node *node, uint64_t offset, size_t size, const void *buffer);
int vfs_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent);
struct vfs_node *vfs_finddir(struct vfs_node *node, const char *name);
int vfs_create(const char *path, uint32_t mode, struct vfs_node **out_node);
int vfs_mkdir(const char *path, uint32_t mode, struct vfs_node **out_node);
int vfs_unlink(const char *path);

/* Ramfs creation helper */
struct vfs_node *vfs_ramfs_create_root(void);

#ifdef DAWN_KERNEL_TEST
bool vfs_self_test(void);
#endif

#endif
