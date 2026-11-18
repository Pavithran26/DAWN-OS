/* SPDX-License-Identifier: Unlicense */
#include "partition.h"

#define PARTITION_MAX_SLICES 64U

static struct partition_info active_slices[PARTITION_MAX_SLICES];
static size_t active_slice_count = 0;
static bool partition_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static inline size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void make_slice_name(char *dest, const char *parent_name, uint32_t index) {
    size_t plen = str_len(parent_name);
    size_t d = 0;
    for (size_t i = 0; i < plen && d + 6 < BLOCK_DEV_NAME_MAX; i++) {
        dest[d++] = parent_name[i];
    }
    if (plen > 0 && is_digit(parent_name[plen - 1])) {
        dest[d++] = 'p';
    }
    if (index >= 100U && d + 1 < BLOCK_DEV_NAME_MAX) {
        dest[d++] = (char)('0' + ((index / 100U) % 10U));
    }
    if (index >= 10U && d + 1 < BLOCK_DEV_NAME_MAX) {
        dest[d++] = (char)('0' + ((index / 10U) % 10U));
    }
    if (d + 1 < BLOCK_DEV_NAME_MAX) {
        dest[d++] = (char)('0' + (index % 10U));
    }
    dest[d] = '\0';
}

/* Slice block device callbacks */
static int slice_read_blocks(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct partition_info *part = (struct partition_info *)dev->driver_data;
    if (lba + count > part->sector_count) return -1;
    return block_dev_read(part->parent_disk, part->start_lba + lba, count, buffer);
}

static int slice_write_blocks(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct partition_info *part = (struct partition_info *)dev->driver_data;
    if (lba + count > part->sector_count) return -1;
    return block_dev_write(part->parent_disk, part->start_lba + lba, count, buffer);
}

static int slice_flush(struct block_device *dev) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct partition_info *part = (struct partition_info *)dev->driver_data;
    if (part->parent_disk && part->parent_disk->ops.flush) {
        return part->parent_disk->ops.flush(part->parent_disk);
    }
    return 0;
}

/* Simple CRC32 for GPT */
static uint32_t crc32(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1U)));
        }
    }
    return ~crc;
}

bool partition_initialize(void) {
    if (partition_initialized) return true;
    for (size_t i = 0; i < PARTITION_MAX_SLICES; i++) {
        active_slices[i] = (struct partition_info){0};
    }
    active_slice_count = 0;
    partition_initialized = true;
    return true;
}

static int register_partition_slice(struct partition_info *part) {
    if (active_slice_count >= PARTITION_MAX_SLICES) return -1;
    struct partition_info *slot = &active_slices[active_slice_count];
    *slot = *part;
    slot->active = true;

    char sname[BLOCK_DEV_NAME_MAX];
    make_slice_name(sname, part->parent_disk->name, part->index);
    str_copy(slot->slice_dev.name, sname, BLOCK_DEV_NAME_MAX);
    slot->slice_dev.type = BLOCK_DEV_TYPE_PARTITION;
    slot->slice_dev.block_size = part->parent_disk->block_size;
    slot->slice_dev.total_blocks = part->sector_count;
    slot->slice_dev.capacity_bytes = part->size_bytes;
    slot->slice_dev.read_only = part->parent_disk->read_only;
    slot->slice_dev.driver_data = slot;
    slot->slice_dev.ops.read_blocks = slice_read_blocks;
    slot->slice_dev.ops.write_blocks = slice_write_blocks;
    slot->slice_dev.ops.flush = slice_flush;

    int id = block_dev_register(&slot->slice_dev);
    if (id < 0) return -1;
    active_slice_count++;
    return id;
}

int partition_scan_disk(struct block_device *disk, struct disk_partition_table *out_table) {
    if (disk == NULL || out_table == NULL) return -1;
    *out_table = (struct disk_partition_table){0};

    uint8_t sector_buf[512];
    if (block_dev_read(disk, 0, 1, sector_buf) != 0) return -1;

    struct mbr_sector *mbr = (struct mbr_sector *)sector_buf;
    if (mbr->signature != 0xAA55U) return 0; /* Unpartitioned or unformatted */

    /* Check for GPT Protective MBR */
    if (mbr->entries[0].type == MBR_TYPE_GPT_PROTECT) {
        /* Read GPT Header at LBA 1 */
        uint8_t gpt_buf[512];
        if (block_dev_read(disk, 1, 1, gpt_buf) == 0) {
            struct gpt_header *gh = (struct gpt_header *)gpt_buf;
            if (gh->signature == GPT_SIGNATURE) {
                out_table->scheme = PARTITION_SCHEME_GPT;

                /* Read partition entries */
                uint8_t entry_buf[512];
                uint64_t entry_lba = gh->partition_entries_lba;
                uint32_t num_entries = gh->number_of_partition_entries;
                if (num_entries > 16) num_entries = 16; /* Inspect up to 16 */

                uint32_t pcount = 0;
                for (uint32_t i = 0; i < num_entries; i++) {
                    uint32_t sector_offset = (i * 128U) / 512U;
                    uint32_t in_sector_offset = (i * 128U) % 512U;

                    if (block_dev_read(disk, entry_lba + sector_offset, 1, entry_buf) != 0) break;
                    struct gpt_entry *ge = (struct gpt_entry *)(entry_buf + in_sector_offset);

                    /* Check if entry is empty (type GUID all zeroes) */
                    bool empty = true;
                    for (int g = 0; g < 16; g++) {
                        if (ge->type_guid[g] != 0) { empty = false; break; }
                    }
                    if (empty) continue;

                    struct partition_info *pi = &out_table->partitions[pcount];
                    pi->index = pcount + 1;
                    pi->start_lba = ge->starting_lba;
                    pi->sector_count = (ge->ending_lba >= ge->starting_lba) ? (ge->ending_lba - ge->starting_lba + 1) : 0;
                    pi->size_bytes = pi->sector_count * disk->block_size;
                    pi->parent_disk = disk;
                    for (int g = 0; g < 16; g++) {
                        pi->type_guid[g] = ge->type_guid[g];
                    }
                    /* Convert UTF-16LE name to ASCII */
                    for (size_t c = 0; c < 35 && ge->partition_name[c] != 0; c++) {
                        pi->name[c] = (char)(ge->partition_name[c] & 0x7FU);
                        pi->name[c + 1] = '\0';
                    }
                    register_partition_slice(pi);
                    pcount++;
                }
                out_table->partition_count = pcount;
                return (int)pcount;
            }
        }
    }

    /* Standard MBR */
    out_table->scheme = PARTITION_SCHEME_MBR;
    uint32_t pcount = 0;
    for (uint32_t i = 0; i < MBR_PARTITION_COUNT; i++) {
        if (mbr->entries[i].type != MBR_TYPE_EMPTY && mbr->entries[i].sector_count > 0) {
            struct partition_info *pi = &out_table->partitions[pcount];
            pi->index = i + 1;
            pi->start_lba = mbr->entries[i].start_lba;
            pi->sector_count = mbr->entries[i].sector_count;
            pi->size_bytes = (uint64_t)pi->sector_count * disk->block_size;
            pi->mbr_type = mbr->entries[i].type;
            pi->bootable = (mbr->entries[i].boot_indicator == 0x80U);
            pi->parent_disk = disk;

            if (pi->mbr_type == MBR_TYPE_FAT32_CHS || pi->mbr_type == MBR_TYPE_FAT32_LBA) {
                str_copy(pi->name, "FAT32", PARTITION_NAME_MAX);
            } else if (pi->mbr_type == MBR_TYPE_DAWN) {
                str_copy(pi->name, "DAWN-OS", PARTITION_NAME_MAX);
            } else {
                str_copy(pi->name, "Primary", PARTITION_NAME_MAX);
            }

            register_partition_slice(pi);
            pcount++;
        }
    }
    out_table->partition_count = pcount;
    return (int)pcount;
}

bool partition_validate_safe_operation(struct block_device *disk, uint64_t start_lba, uint64_t count) {
    if (disk == NULL || count == 0) return false;
    /* Reserve first 2048 sectors (1MB) for partition table and boot code */
    if (start_lba < 2048U) return false;
    if (start_lba + count > disk->total_blocks) return false;

    /* Check overlap against registered slices */
    for (size_t i = 0; i < active_slice_count; i++) {
        if (active_slices[i].active && active_slices[i].parent_disk == disk) {
            uint64_t existing_start = active_slices[i].start_lba;
            uint64_t existing_end = existing_start + active_slices[i].sector_count;
            uint64_t new_end = start_lba + count;

            if (start_lba < existing_end && new_end > existing_start) {
                return false; /* Overlap detected */
            }
        }
    }
    return true;
}

int partition_create_mbr(struct block_device *disk, uint32_t index, uint64_t start_lba, uint64_t count, uint8_t type, bool bootable) {
    if (disk == NULL || index == 0 || index > MBR_PARTITION_COUNT) return -1;
    if (!partition_validate_safe_operation(disk, start_lba, count)) return -1;

    uint8_t sector_buf[512] = {0};
    if (block_dev_read(disk, 0, 1, sector_buf) != 0) return -1;

    struct mbr_sector *mbr = (struct mbr_sector *)sector_buf;
    mbr->entries[index - 1].boot_indicator = bootable ? 0x80U : 0x00U;
    mbr->entries[index - 1].type = type;
    mbr->entries[index - 1].start_lba = (uint32_t)start_lba;
    mbr->entries[index - 1].sector_count = (uint32_t)count;
    mbr->signature = 0xAA55U;

    if (block_dev_write(disk, 0, 1, sector_buf) != 0) return -1;

    struct partition_info pi = {0};
    pi.index = index;
    pi.start_lba = start_lba;
    pi.sector_count = count;
    pi.size_bytes = count * disk->block_size;
    pi.mbr_type = type;
    pi.bootable = bootable;
    pi.parent_disk = disk;
    str_copy(pi.name, "Primary", PARTITION_NAME_MAX);
    return register_partition_slice(&pi);
}

int partition_delete(struct block_device *disk, uint32_t index) {
    if (disk == NULL || index == 0 || index > MBR_PARTITION_COUNT) return -1;

    uint8_t sector_buf[512] = {0};
    if (block_dev_read(disk, 0, 1, sector_buf) != 0) return -1;

    struct mbr_sector *mbr = (struct mbr_sector *)sector_buf;
    mbr->entries[index - 1] = (struct mbr_entry){0};

    if (block_dev_write(disk, 0, 1, sector_buf) != 0) return -1;

    /* Invalidate active slice */
    for (size_t i = 0; i < active_slice_count; i++) {
        if (active_slices[i].active && active_slices[i].parent_disk == disk && active_slices[i].index == index) {
            active_slices[i].active = false;
            block_dev_unregister(active_slices[i].slice_dev.id);
            break;
        }
    }
    return 0;
}

int partition_format_fat32(struct block_device *part_dev, const char *volume_label) {
    if (part_dev == NULL || part_dev->total_blocks < 65536U) return -1; /* Min FAT32 size */

    uint8_t bpb_sector[512] = {0};
    /* Jump boot code */
    bpb_sector[0] = 0xEBU;
    bpb_sector[1] = 0x58U;
    bpb_sector[2] = 0x90U;
    str_copy((char *)&bpb_sector[3], "MSWIN4.1", 8);

    /* BPB fields */
    uint16_t bytes_per_sec = (uint16_t)part_dev->block_size;
    bpb_sector[11] = (uint8_t)(bytes_per_sec & 0xFFU);
    bpb_sector[12] = (uint8_t)((bytes_per_sec >> 8) & 0xFFU);
    bpb_sector[13] = 8; /* 8 sectors per cluster (4KB) */
    bpb_sector[14] = 32; /* 32 reserved sectors */
    bpb_sector[15] = 0;
    bpb_sector[16] = 2; /* 2 FATs */
    bpb_sector[21] = 0xF8U; /* Media descriptor: Fixed disk */

    /* Total sectors 32 */
    uint32_t total_sec = (uint32_t)part_dev->total_blocks;
    bpb_sector[32] = (uint8_t)(total_sec & 0xFFU);
    bpb_sector[33] = (uint8_t)((total_sec >> 8) & 0xFFU);
    bpb_sector[34] = (uint8_t)((total_sec >> 16) & 0xFFU);
    bpb_sector[35] = (uint8_t)((total_sec >> 24) & 0xFFU);

    /* FAT32 Extended fields */
    uint32_t fat_size = (total_sec / 8U * 4U + 511U) / 512U;
    bpb_sector[36] = (uint8_t)(fat_size & 0xFFU);
    bpb_sector[37] = (uint8_t)((fat_size >> 8) & 0xFFU);
    bpb_sector[38] = (uint8_t)((fat_size >> 16) & 0xFFU);
    bpb_sector[39] = (uint8_t)((fat_size >> 24) & 0xFFU);
    bpb_sector[44] = 2; /* Root cluster = 2 */
    bpb_sector[45] = 0;
    bpb_sector[46] = 0;
    bpb_sector[47] = 0;

    bpb_sector[66] = 0x29U; /* Boot signature */
    str_copy((char *)&bpb_sector[71], volume_label ? volume_label : "DAWN_VOL   ", 11);
    str_copy((char *)&bpb_sector[82], "FAT32   ", 8);
    bpb_sector[510] = 0x55U;
    bpb_sector[511] = 0xAAU;

    if (block_dev_write(part_dev, 0, 1, bpb_sector) != 0) return -1;

    /* Write FAT table headers */
    uint8_t fat_header[512] = {0};
    fat_header[0] = 0xF8U; /* Cluster 0: Media byte */
    fat_header[1] = 0xFFU;
    fat_header[2] = 0xFFU;
    fat_header[3] = 0x0FU;
    fat_header[4] = 0xFFU; /* Cluster 1: EOF marker */
    fat_header[5] = 0xFFU;
    fat_header[6] = 0xFFU;
    fat_header[7] = 0x0FU;
    fat_header[8] = 0xF8U; /* Cluster 2 (Root Dir): EOF */
    fat_header[9] = 0xFFU;
    fat_header[10] = 0xFFU;
    fat_header[11] = 0x0FU;

    /* Write to FAT1 (LBA 32) and FAT2 (LBA 32 + fat_size) */
    (void)block_dev_write(part_dev, 32, 1, fat_header);
    (void)block_dev_write(part_dev, 32 + fat_size, 1, fat_header);

    return 0;
}

/* Self-test */
static uint8_t test_disk_storage[512 * 8192]; /* 4MB RAM disk */

bool partition_self_test(void) {
    if (!partition_initialize()) return false;
    for (size_t i = 0; i < sizeof(test_disk_storage); i++) {
        test_disk_storage[i] = 0;
    }

    struct block_device *disk = ramdisk_create("test_mbr_disk", 512, 8192, test_disk_storage);
    if (disk == NULL) return false;

    /* Safe operation validation */
    /* Under 2048 should be rejected (reserved for MBR/GPT) */
    if (partition_validate_safe_operation(disk, 100, 1000)) return false;
    /* Beyond disk total blocks should be rejected */
    if (partition_validate_safe_operation(disk, 2048, 8000)) return false;
    /* Valid range: 2048 to 4096 */
    if (!partition_validate_safe_operation(disk, 2048, 2048)) return false;

    /* Create MBR partition 1: LBA 2048, 2048 blocks, type FAT32 */
    int pid = partition_create_mbr(disk, 1, 2048, 2048, MBR_TYPE_FAT32_LBA, true);
    if (pid < 0) return false;

    /* Overlap test: creating overlapping partition should fail */
    if (partition_create_mbr(disk, 2, 3000, 1000, MBR_TYPE_DAWN, false) >= 0) return false;

    /* Create non-overlapping partition 2: LBA 4096, 2048 blocks */
    int pid2 = partition_create_mbr(disk, 2, 4096, 2048, MBR_TYPE_DAWN, false);
    if (pid2 < 0) return false;

    /* Scan disk */
    struct disk_partition_table table = {0};
    int found = partition_scan_disk(disk, &table);
    if (found != 2) return false;
    if (table.scheme != PARTITION_SCHEME_MBR) return false;
    if (table.partitions[0].start_lba != 2048 || table.partitions[0].sector_count != 2048) return false;
    if (table.partitions[1].start_lba != 4096 || table.partitions[1].sector_count != 2048) return false;

    /* Slice device I/O test */
    struct block_device *slice1 = block_dev_lookup("test_mbr_disk1");
    if (slice1 == NULL) return false;
    if (slice1->total_blocks != 2048 || slice1->capacity_bytes != 1048576) return false;

    uint8_t write_pat[512];
    for (size_t i = 0; i < 512; i++) write_pat[i] = 0xA5U;
    if (block_dev_write(slice1, 0, 1, write_pat) != 0) return false;

    /* Verify written to disk offset 2048 */
    uint8_t verify_buf[512] = {0};
    if (block_dev_read(disk, 2048, 1, verify_buf) != 0) return false;
    for (size_t i = 0; i < 512; i++) {
        if (verify_buf[i] != 0xA5U) return false;
    }

    /* Out of bounds test on slice */
    if (block_dev_write(slice1, 2048, 1, write_pat) == 0) return false;

    /* Delete partition 2 */
    if (partition_delete(disk, 2) != 0) return false;

    /* CRC32 test */
    const char *test_str = "123456789";
    uint32_t c = crc32(test_str, 9);
    if (c != 0xCBF43926U) return false;

    /* Cleanup */
    block_dev_unregister(disk->id);
    return true;
}
