/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PARTITION_H
#define DAWN_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_dev.h"

#define MBR_PARTITION_COUNT 4U
#define GPT_MAX_PARTITIONS  128U
#define PARTITION_NAME_MAX  36U

#define MBR_TYPE_EMPTY       0x00U
#define MBR_TYPE_FAT32_CHS   0x0BU
#define MBR_TYPE_FAT32_LBA   0x0CU
#define MBR_TYPE_LINUX       0x83U
#define MBR_TYPE_GPT_PROTECT 0xEEU
#define MBR_TYPE_EFI         0xEFU
#define MBR_TYPE_DAWN        0xDAU

#define GPT_SIGNATURE 0x5452415020494645ULL /* "EFI PART" */

struct mbr_entry {
    uint8_t  boot_indicator; /* 0x80 = active */
    uint8_t  start_chs[3];
    uint8_t  type;
    uint8_t  end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed));

_Static_assert(sizeof(struct mbr_entry) == 16, "MBR entry must be 16 bytes");

struct mbr_sector {
    uint8_t bootstrap[446];
    struct mbr_entry entries[MBR_PARTITION_COUNT];
    uint16_t signature; /* 0xAA55 */
} __attribute__((packed));

_Static_assert(sizeof(struct mbr_sector) == 512, "MBR sector must be 512 bytes");

struct gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t number_of_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_array_crc32;
} __attribute__((packed));

_Static_assert(sizeof(struct gpt_header) == 92, "GPT header must be 92 bytes");

struct gpt_entry {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36]; /* UTF-16LE */
} __attribute__((packed));

_Static_assert(sizeof(struct gpt_entry) == 128, "GPT entry must be 128 bytes");

enum partition_scheme {
    PARTITION_SCHEME_UNKNOWN = 0,
    PARTITION_SCHEME_MBR = 1,
    PARTITION_SCHEME_GPT = 2
};

struct partition_info {
    uint32_t index;
    uint64_t start_lba;
    uint64_t sector_count;
    uint64_t size_bytes;
    uint8_t  mbr_type;
    uint8_t  type_guid[16];
    char     name[PARTITION_NAME_MAX];
    bool     bootable;
    bool     active;
    struct block_device *parent_disk;
    struct block_device slice_dev;
};

struct disk_partition_table {
    enum partition_scheme scheme;
    uint32_t partition_count;
    struct partition_info partitions[MBR_PARTITION_COUNT > 16 ? MBR_PARTITION_COUNT : 16];
};

bool partition_initialize(void);
int partition_scan_disk(struct block_device *disk, struct disk_partition_table *out_table);
bool partition_validate_safe_operation(struct block_device *disk, uint64_t start_lba, uint64_t count);
int partition_create_mbr(struct block_device *disk, uint32_t index, uint64_t start_lba, uint64_t count, uint8_t type, bool bootable);
int partition_create_gpt(struct block_device *disk, uint32_t index, uint64_t start_lba, uint64_t count, const uint8_t type_guid[16], const char *name);
int partition_delete(struct block_device *disk, uint32_t index);
int partition_format_fat32(struct block_device *part_dev, const char *volume_label);

bool partition_self_test(void);

#endif /* DAWN_PARTITION_H */
