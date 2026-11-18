/* SPDX-License-Identifier: Unlicense */
#include "storage_manager.h"
#include "ahci.h"
#include "nvme.h"
#include "vfs.h"
#include "fat32.h"
#include "iso9660.h"

static struct storage_disk_desc discovered_disks[STORAGE_MAX_DISKS];
static size_t discovered_disk_count = 0;
static bool storage_mgr_initialized = false;

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

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
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

static void refresh_disk_partitions(struct storage_disk_desc *desc, struct block_device *disk) {
    struct disk_partition_table ptable = {0};
    int pcount = partition_scan_disk(disk, &ptable);
    desc->scheme = ptable.scheme;
    desc->partition_count = (pcount > 0) ? (uint32_t)pcount : 0;

    for (uint32_t i = 0; i < desc->partition_count && i < STORAGE_MAX_PARTITIONS_PER_DISK; i++) {
        struct storage_partition_desc *sp = &desc->partitions[i];
        *sp = (struct storage_partition_desc){0};
        sp->index = ptable.partitions[i].index;
        sp->start_lba = ptable.partitions[i].start_lba;
        sp->sector_count = ptable.partitions[i].sector_count;
        sp->capacity_bytes = ptable.partitions[i].size_bytes;
        sp->mbr_type = ptable.partitions[i].mbr_type;
        make_slice_name(sp->slice_name, disk->name, sp->index);

        if (sp->mbr_type == MBR_TYPE_FAT32_CHS || sp->mbr_type == MBR_TYPE_FAT32_LBA) {
            str_copy(sp->fs_name, "FAT32", sizeof(sp->fs_name));
        } else if (sp->mbr_type == MBR_TYPE_DAWN) {
            str_copy(sp->fs_name, "DAWN-FS", sizeof(sp->fs_name));
        } else {
            str_copy(sp->fs_name, "RAW", sizeof(sp->fs_name));
        }
    }
}

bool storage_manager_initialize(void) {
    if (storage_mgr_initialized) return true;

    block_dev_initialize();
    pci_initialize();
    ahci_initialize();
    nvme_initialize();
    partition_initialize();

    for (size_t i = 0; i < STORAGE_MAX_DISKS; i++) {
        discovered_disks[i] = (struct storage_disk_desc){0};
    }
    discovered_disk_count = 0;

    /* Enumerate block devices that represent physical disks */
    size_t dev_count = block_dev_count();
    for (size_t i = 0; i < dev_count && discovered_disk_count < STORAGE_MAX_DISKS; i++) {
        struct block_device *bdev = block_dev_get_by_index(i);
        if (bdev == NULL) continue;

        if (bdev->type == BLOCK_DEV_TYPE_SATA || bdev->type == BLOCK_DEV_TYPE_NVME || bdev->type == BLOCK_DEV_TYPE_OPTICAL) {
            struct storage_disk_desc *desc = &discovered_disks[discovered_disk_count];
            *desc = (struct storage_disk_desc){0};
            str_copy(desc->name, bdev->name, BLOCK_DEV_NAME_MAX);
            str_copy(desc->model, (bdev->type == BLOCK_DEV_TYPE_SATA) ? "SATA Mass Storage" :
                                  (bdev->type == BLOCK_DEV_TYPE_NVME) ? "NVMe Solid State Drive" : "Optical Media", sizeof(desc->model));
            desc->type = bdev->type;
            desc->block_size = bdev->block_size;
            desc->total_blocks = bdev->total_blocks;
            desc->capacity_bytes = bdev->capacity_bytes;
            desc->read_only = bdev->read_only;

            refresh_disk_partitions(desc, bdev);
            discovered_disk_count++;
        }
    }

    storage_mgr_initialized = true;
    return true;
}

size_t storage_get_disk_count(void) {
    if (!storage_mgr_initialized) storage_manager_initialize();
    return discovered_disk_count;
}

const struct storage_disk_desc *storage_get_disk_info(size_t index) {
    if (!storage_mgr_initialized) storage_manager_initialize();
    if (index >= discovered_disk_count) return NULL;
    return &discovered_disks[index];
}

const struct storage_disk_desc *storage_find_disk(const char *name) {
    if (!storage_mgr_initialized) storage_manager_initialize();
    if (name == NULL) return NULL;
    for (size_t i = 0; i < discovered_disk_count; i++) {
        if (str_equal(discovered_disks[i].name, name)) {
            return &discovered_disks[i];
        }
    }
    return NULL;
}

int storage_create_partition(const char *disk_name, uint64_t size_blocks, uint8_t mbr_type, char *out_slice_name) {
    if (disk_name == NULL || size_blocks == 0) return -1;
    struct block_device *disk = block_dev_lookup(disk_name);
    if (disk == NULL) return -1;

    struct storage_disk_desc *desc = NULL;
    for (size_t i = 0; i < discovered_disk_count; i++) {
        if (str_equal(discovered_disks[i].name, disk_name)) {
            desc = &discovered_disks[i];
            break;
        }
    }
    if (desc == NULL) return -1;

    /* Find next partition index */
    uint32_t next_idx = 1;
    uint64_t next_lba = 2048; /* Start at 1MB */
    for (uint32_t i = 0; i < desc->partition_count; i++) {
        if (desc->partitions[i].index >= next_idx) {
            next_idx = desc->partitions[i].index + 1;
        }
        uint64_t end = desc->partitions[i].start_lba + desc->partitions[i].sector_count;
        if (end > next_lba) {
            /* Align to 2048 sectors */
            next_lba = (end + 2047U) & ~2047ULL;
        }
    }

    if (next_idx > MBR_PARTITION_COUNT) return -1; /* MBR table full */

    int pid = partition_create_mbr(disk, next_idx, next_lba, size_blocks, mbr_type, (next_idx == 1));
    if (pid < 0) return -1;

    refresh_disk_partitions(desc, disk);
    if (out_slice_name != NULL) {
        make_slice_name(out_slice_name, disk->name, next_idx);
    }
    return 0;
}

int storage_delete_partition(const char *disk_name, uint32_t partition_index) {
    if (disk_name == NULL || partition_index == 0) return -1;
    struct block_device *disk = block_dev_lookup(disk_name);
    if (disk == NULL) return -1;

    int ret = partition_delete(disk, partition_index);
    if (ret != 0) return -1;

    for (size_t i = 0; i < discovered_disk_count; i++) {
        if (str_equal(discovered_disks[i].name, disk_name)) {
            refresh_disk_partitions(&discovered_disks[i], disk);
            break;
        }
    }
    return 0;
}

int storage_format_partition(const char *slice_name, const char *fs_type, const char *label) {
    if (slice_name == NULL || fs_type == NULL) return -1;
    struct block_device *part = block_dev_lookup(slice_name);
    if (part == NULL) return -1;

    if (str_equal(fs_type, "fat32") || str_equal(fs_type, "FAT32")) {
        return partition_format_fat32(part, label);
    }
    return -1;
}

int storage_automount_filesystems(void) {
    if (!storage_mgr_initialized) storage_manager_initialize();
    int mounted_count = 0;

    for (size_t d = 0; d < discovered_disk_count; d++) {
        struct storage_disk_desc *disk = &discovered_disks[d];
        for (uint32_t p = 0; p < disk->partition_count; p++) {
            struct storage_partition_desc *part = &disk->partitions[p];
            if (part->mounted) continue;

            struct block_device *bdev = block_dev_lookup(part->slice_name);
            if (bdev == NULL) continue;

            if (part->mbr_type == MBR_TYPE_FAT32_CHS || part->mbr_type == MBR_TYPE_FAT32_LBA) {
                char mnt_path[64];
                mnt_path[0] = '/';
                mnt_path[1] = 'm';
                mnt_path[2] = 'n';
                mnt_path[3] = 't';
                mnt_path[4] = '/';
                str_copy(&mnt_path[5], part->slice_name, 58);

                /* Create mount point in VFS and mount */
                (void)vfs_mkdir(mnt_path, 0755, NULL);
                if (bdev->driver_data != NULL) {
                    struct vfs_node *fat_root = fat32_mount_image(bdev->driver_data, (size_t)bdev->capacity_bytes);
                    if (fat_root != NULL) {
                        if (vfs_mount(mnt_path, fat_root, "fat32")) {
                            part->mounted = true;
                            str_copy(part->mount_point, mnt_path, sizeof(part->mount_point));
                            mounted_count++;
                        }
                    }
                }
            }
        }
    }
    return mounted_count;
}

/* Self-test */
static uint8_t test_mgr_disk_storage[512 * 8192]; /* 4MB RAM disk */

bool storage_manager_self_test(void) {
    if (!storage_manager_initialize()) return false;
    for (size_t i = 0; i < sizeof(test_mgr_disk_storage); i++) {
        test_mgr_disk_storage[i] = 0;
    }

    struct block_device *disk = ramdisk_create("sda", 512, 8192, test_mgr_disk_storage);
    if (disk == NULL) return false;
    disk->type = BLOCK_DEV_TYPE_SATA;

    /* Add disk to discovered list for test */
    size_t prev_count = discovered_disk_count;
    if (discovered_disk_count < STORAGE_MAX_DISKS) {
        struct storage_disk_desc *desc = &discovered_disks[discovered_disk_count++];
        *desc = (struct storage_disk_desc){0};
        str_copy(desc->name, "sda", BLOCK_DEV_NAME_MAX);
        str_copy(desc->model, "DAWN-OS SATA Drive", sizeof(desc->model));
        desc->type = BLOCK_DEV_TYPE_SATA;
        desc->block_size = 512;
        desc->total_blocks = 8192;
        desc->capacity_bytes = 4194304;

        /* Create partition on sda */
        char slice_name[BLOCK_DEV_NAME_MAX] = {0};
        int res = storage_create_partition("sda", 2048, MBR_TYPE_FAT32_LBA, slice_name);
        if (res != 0) return false;
        if (!str_equal(slice_name, "sda1")) return false;

        /* Query disk info */
        const struct storage_disk_desc *qdisk = storage_find_disk("sda");
        if (qdisk == NULL || qdisk->partition_count != 1) return false;
        if (!str_equal(qdisk->partitions[0].slice_name, "sda1")) return false;
        if (qdisk->partitions[0].capacity_bytes != 1048576) return false;

        /* Delete partition */
        if (storage_delete_partition("sda", 1) != 0) return false;
        if (qdisk->partition_count != 0) return false;

        /* Restore discovered count */
        discovered_disk_count = prev_count;
    }

    block_dev_unregister(disk->id);
    return true;
}
