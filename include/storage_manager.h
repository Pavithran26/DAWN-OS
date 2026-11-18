/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_STORAGE_MANAGER_H
#define DAWN_STORAGE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_dev.h"
#include "partition.h"

#define STORAGE_MAX_DISKS 8U
#define STORAGE_MAX_PARTITIONS_PER_DISK 8U

struct storage_partition_desc {
    char slice_name[BLOCK_DEV_NAME_MAX];
    uint32_t index;
    uint64_t start_lba;
    uint64_t sector_count;
    uint64_t capacity_bytes;
    uint8_t  mbr_type;
    char     fs_name[16];
    bool     mounted;
    char     mount_point[64];
};

struct storage_disk_desc {
    char name[BLOCK_DEV_NAME_MAX];
    char model[41];
    enum block_dev_type type;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t capacity_bytes;
    enum partition_scheme scheme;
    uint32_t partition_count;
    struct storage_partition_desc partitions[STORAGE_MAX_PARTITIONS_PER_DISK];
    bool read_only;
};

bool storage_manager_initialize(void);
size_t storage_get_disk_count(void);
const struct storage_disk_desc *storage_get_disk_info(size_t index);
const struct storage_disk_desc *storage_find_disk(const char *name);
int storage_create_partition(const char *disk_name, uint64_t size_blocks, uint8_t mbr_type, char *out_slice_name);
int storage_delete_partition(const char *disk_name, uint32_t partition_index);
int storage_format_partition(const char *slice_name, const char *fs_type, const char *label);
int storage_automount_filesystems(void);

bool storage_manager_self_test(void);

#endif /* DAWN_STORAGE_MANAGER_H */
