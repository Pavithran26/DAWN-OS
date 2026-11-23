/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_ISO9660_H
#define DAWN_ISO9660_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define ISO9660_SECTOR_SIZE 2048U
#define ISO9660_PVD_SECTOR  16U

#define ISO9660_FLAG_HIDDEN    0x01U
#define ISO9660_FLAG_DIRECTORY 0x02U

struct iso9660_pvd {
    uint8_t type;
    char id[5]; /* "CD001" */
    uint8_t version;
    uint8_t unused1;
    char system_id[32];
    char volume_id[32];
    uint8_t unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_le;
    uint32_t opt_path_table_le;
    uint32_t path_table_be;
    uint32_t opt_path_table_be;
    uint8_t root_directory_record[34];
    char volume_set_id[128];
    char publisher_id[128];
    char preparer_id[128];
    char application_id[128];
} __attribute__((packed));

struct iso9660_dirent {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t recording_date[7];
    uint8_t file_flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint8_t name_len;
    char name[1];
} __attribute__((packed));

struct block_device;
struct vfs_node *iso9660_mount_image(void *image_data, size_t image_size);
struct vfs_node *iso9660_mount_device(struct block_device *bdev);

#ifdef DAWN_KERNEL_TEST
bool iso9660_self_test(void);
bool iso9660_live_device_test(void);
#endif

#endif
