/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_FAT32_H
#define DAWN_FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"
#include "block_dev.h"

#define FAT32_ATTR_READ_ONLY 0x01U
#define FAT32_ATTR_HIDDEN    0x02U
#define FAT32_ATTR_SYSTEM    0x04U
#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_ARCHIVE   0x20U
#define FAT32_ATTR_LFN       0x0FU

struct fat32_bpb {
    uint8_t jmp_boot[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t table_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t head_side_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;
    uint32_t table_size_32;
    uint16_t extended_flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fat_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fat_type_label[8];
} __attribute__((packed));

struct fat32_dirent {
    char name[11];
    uint8_t attributes;
    uint8_t reserved_nt;
    uint8_t creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct vfs_node *fat32_mount_image(void *image_data, size_t image_size);
bool fat32_mount(struct block_device *bdev, struct vfs_node **out_root);
int fat32_truncate(struct vfs_node *node, uint64_t new_size);

#ifdef DAWN_KERNEL_TEST
bool fat32_self_test(void);
#endif

#endif
