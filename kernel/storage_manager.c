/* SPDX-License-Identifier: Unlicense */
#include "storage_manager.h"
#include "ahci.h"
#include "nvme.h"
#include "vfs.h"
#include "fat32.h"
#include "iso9660.h"
#include "virtio_blk.h"
#include "gfx.h"
#include "window_manager.h"

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
    virtio_blk_initialize();
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

        if (bdev->type == BLOCK_DEV_TYPE_SATA || bdev->type == BLOCK_DEV_TYPE_NVME ||
            bdev->type == BLOCK_DEV_TYPE_OPTICAL || bdev->type == BLOCK_DEV_TYPE_VIRTIO) {
            struct storage_disk_desc *desc = &discovered_disks[discovered_disk_count];
            *desc = (struct storage_disk_desc){0};
            str_copy(desc->name, bdev->name, BLOCK_DEV_NAME_MAX);
            str_copy(desc->model, (bdev->type == BLOCK_DEV_TYPE_SATA) ? "SATA Mass Storage" :
                                  (bdev->type == BLOCK_DEV_TYPE_NVME) ? "NVMe Solid State Drive" :
                                  (bdev->type == BLOCK_DEV_TYPE_OPTICAL) ? "Optical Media" :
                                  "VirtIO Block Device", sizeof(desc->model));
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
    if (disk_name == NULL || size_blocks == 0) return STORAGE_ERR_INVALID;
    struct block_device *disk = block_dev_lookup(disk_name);
    if (disk == NULL) return STORAGE_ERR_INVALID;
    if (disk->read_only) return STORAGE_ERR_READ_ONLY;

    struct storage_disk_desc *desc = NULL;
    for (size_t i = 0; i < discovered_disk_count; i++) {
        if (str_equal(discovered_disks[i].name, disk_name)) {
            desc = &discovered_disks[i];
            break;
        }
    }
    if (desc == NULL) return STORAGE_ERR_INVALID;
    if (desc->read_only) return STORAGE_ERR_READ_ONLY;

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

    if (next_idx > MBR_PARTITION_COUNT) return STORAGE_ERR_TABLE_FULL; /* MBR table full */

    /* Bounds check against disk total blocks */
    if (next_lba >= disk->total_blocks || size_blocks > disk->total_blocks - next_lba) {
        return STORAGE_ERR_BOUNDS;
    }

    int pid = partition_create_mbr(disk, next_idx, next_lba, size_blocks, mbr_type, (next_idx == 1));
    if (pid < 0) return STORAGE_ERR_INVALID;

    refresh_disk_partitions(desc, disk);
    if (out_slice_name != NULL) {
        make_slice_name(out_slice_name, disk->name, next_idx);
    }
    return STORAGE_OK;
}

int storage_delete_partition(const char *disk_name, uint32_t partition_index) {
    if (disk_name == NULL || partition_index == 0) return STORAGE_ERR_INVALID;
    struct block_device *disk = block_dev_lookup(disk_name);
    if (disk == NULL) return STORAGE_ERR_INVALID;
    if (disk->read_only) return STORAGE_ERR_READ_ONLY;

    struct storage_disk_desc *desc = NULL;
    for (size_t i = 0; i < discovered_disk_count; i++) {
        if (str_equal(discovered_disks[i].name, disk_name)) {
            desc = &discovered_disks[i];
            break;
        }
    }
    if (desc != NULL && desc->read_only) return STORAGE_ERR_READ_ONLY;

    int ret = partition_delete(disk, partition_index);
    if (ret != 0) return STORAGE_ERR_INVALID;

    if (desc != NULL) {
        refresh_disk_partitions(desc, disk);
    }
    return STORAGE_OK;
}

int storage_format_partition(const char *slice_name, const char *fs_type, const char *label) {
    if (slice_name == NULL || fs_type == NULL) return STORAGE_ERR_INVALID;
    struct block_device *part = block_dev_lookup(slice_name);
    if (part == NULL) return STORAGE_ERR_INVALID;
    if (part->read_only) return STORAGE_ERR_READ_ONLY;

    if (str_equal(fs_type, "fat32") || str_equal(fs_type, "FAT32")) {
        return partition_format_fat32(part, label);
    }
    return STORAGE_ERR_INVALID;
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
                struct vfs_node *fat_root = NULL;
                if (fat32_mount(bdev, &fat_root)) {
                    if (vfs_mount(mnt_path, fat_root, "fat32")) {
                        part->mounted = true;
                        str_copy(part->mount_point, mnt_path, sizeof(part->mount_point));
                        mounted_count++;
                    }
                }
            }
        }
    }
    return mounted_count;
}

/* ========================================================================= */
/* Disks GUI Application (Milestone D5)                                      */
/* ========================================================================= */

static struct window *disks_win = NULL;
static size_t disks_selected_idx = 0;
static uint32_t disks_render_buf[DISKS_WIDTH * DISKS_HEIGHT];

static void format_disk_capacity(uint64_t bytes, char *out, size_t max) {
    if (out == NULL || max == 0) return;
    if (bytes < 1024ULL) {
        size_t pos = 0;
        char num[32];
        size_t nlen = 0;
        uint64_t v = bytes;
        if (v == 0) { num[nlen++] = '0'; }
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 3 < max; i++) out[pos++] = num[i];
        out[pos++] = ' '; out[pos++] = 'B'; out[pos] = '\0';
    } else if (bytes < 1024ULL * 1024ULL) {
        uint64_t kb = bytes / 1024ULL;
        uint64_t frac = (bytes % 1024ULL) * 10ULL / 1024ULL;
        size_t pos = 0;
        char num[32]; size_t nlen = 0; uint64_t v = kb;
        if (v == 0) num[nlen++] = '0';
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 6 < max; i++) out[pos++] = num[i];
        out[pos++] = '.';
        out[pos++] = (char)('0' + (frac % 10));
        out[pos++] = ' '; out[pos++] = 'K'; out[pos++] = 'B'; out[pos] = '\0';
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        uint64_t mb = bytes / (1024ULL * 1024ULL);
        uint64_t frac = ((bytes % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL);
        size_t pos = 0;
        char num[32]; size_t nlen = 0; uint64_t v = mb;
        if (v == 0) num[nlen++] = '0';
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 6 < max; i++) out[pos++] = num[i];
        out[pos++] = '.';
        out[pos++] = (char)('0' + (frac % 10));
        out[pos++] = ' '; out[pos++] = 'M'; out[pos++] = 'B'; out[pos] = '\0';
    } else {
        uint64_t gb = bytes / (1024ULL * 1024ULL * 1024ULL);
        uint64_t frac = ((bytes % (1024ULL * 1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL * 1024ULL);
        size_t pos = 0;
        char num[32]; size_t nlen = 0; uint64_t v = gb;
        if (v == 0) num[nlen++] = '0';
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 6 < max; i++) out[pos++] = num[i];
        out[pos++] = '.';
        out[pos++] = (char)('0' + (frac % 10));
        out[pos++] = ' '; out[pos++] = 'G'; out[pos++] = 'B'; out[pos] = '\0';
    }
}

static void u64_to_buf(uint64_t val, char *out, size_t max) {
    if (out == NULL || max == 0) return;
    if (val == 0) {
        if (max > 1) { out[0] = '0'; out[1] = '\0'; }
        return;
    }
    char tmp[32]; size_t t = 0;
    while (val > 0 && t < sizeof(tmp)) {
        tmp[t++] = (char)('0' + (val % 10));
        val /= 10;
    }
    size_t pos = 0;
    while (t > 0 && pos + 1 < max) {
        out[pos++] = tmp[--t];
    }
    out[pos] = '\0';
}

void disks_select_disk(size_t index) {
    if (index < discovered_disk_count) {
        disks_selected_idx = index;
    }
}

void disks_window_render(void) {
    struct gfx_surface surf = {
        .buffer = disks_render_buf,
        .width = DISKS_WIDTH,
        .height = DISKS_HEIGHT,
        .pitch = DISKS_WIDTH * sizeof(uint32_t),
        .buffer_size = sizeof(disks_render_buf),
        .is_hardware_fb = false,
        .format = (uint8_t)GFX_FORMAT_ARGB8888
    };

    /* Background: Deep emerald base */
    gfx_clear(&surf, GFX_COLOR_EMERALD_BASE);

    /* Storage Safety Gate Top Banner (y: 6..28, h: 22) */
    gfx_draw_glass_panel(&surf, 6, 6, DISKS_WIDTH - 12, 22, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, 6, 6, DISKS_WIDTH - 12, 22, GFX_COLOR_WARNING);
    gfx_draw_string(&surf, 12, 9, "[RO] Storage Safety Gate Active: Host Disks Write-Protected", GFX_COLOR_WARNING);

    /* Left Pane: Disks List (x: 6..186, y: 34..354) */
    gfx_draw_glass_panel(&surf, 6, 34, 180, 320, GFX_COLOR_GLASS_SURFACE);
    gfx_draw_rect_outline(&surf, 6, 34, 180, 320, GFX_COLOR_GLASS_BORDER);

    char dhdr[32];
    str_copy(dhdr, "DISKS (", sizeof(dhdr));
    char cnt_str[16];
    u64_to_buf((uint64_t)discovered_disk_count, cnt_str, sizeof(cnt_str));
    size_t dlen = str_len(dhdr);
    for (size_t c = 0; cnt_str[c] != '\0' && dlen + 2 < sizeof(dhdr); c++) dhdr[dlen++] = cnt_str[c];
    dhdr[dlen++] = ')'; dhdr[dlen] = '\0';
    gfx_draw_string(&surf, 12, 40, dhdr, GFX_COLOR_EMERALD_HIGHLIGHT);

    for (size_t i = 0; i < discovered_disk_count && i < 6U; i++) {
        uint32_t y = 58U + (uint32_t)i * 44U;
        if (i == disks_selected_idx) {
            gfx_draw_rect_filled(&surf, 10, y, 172, 40, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_rect_outline(&surf, 10, y, 172, 40, GFX_COLOR_EMERALD_ACCENT);
        } else {
            gfx_draw_rect_filled(&surf, 10, y, 172, 40, 0x400A251BU);
            gfx_draw_rect_outline(&surf, 10, y, 172, 40, GFX_COLOR_GLASS_BORDER);
        }
        char dev_label[32];
        dev_label[0] = '/'; dev_label[1] = 'd'; dev_label[2] = 'e'; dev_label[3] = 'v'; dev_label[4] = '/';
        str_copy(&dev_label[5], discovered_disks[i].name, 26);
        gfx_draw_string(&surf, 14, y + 4, dev_label, GFX_COLOR_TEXT_PRIMARY);

        char cap_str[32];
        format_disk_capacity(discovered_disks[i].capacity_bytes, cap_str, sizeof(cap_str));
        char sub_label[48];
        str_copy(sub_label, cap_str, sizeof(sub_label));
        size_t slen = str_len(sub_label);
        const char *ro_tag = " [RO]";
        for (size_t r = 0; ro_tag[r] != '\0' && slen + 1 < sizeof(sub_label); r++) sub_label[slen++] = ro_tag[r];
        sub_label[slen] = '\0';
        gfx_draw_string(&surf, 14, y + 20, sub_label, GFX_COLOR_TEXT_SECONDARY);
    }

    /* Right Pane: Selected Disk Details & Partition Slices (x: 192..574, y: 34..354) */
    gfx_draw_glass_panel(&surf, 192, 34, 382, 320, GFX_COLOR_GLASS_SURFACE);
    gfx_draw_rect_outline(&surf, 192, 34, 382, 320, GFX_COLOR_GLASS_BORDER);

    if (discovered_disk_count == 0) {
        gfx_draw_string(&surf, 204, 50, "No block storage devices detected.", GFX_COLOR_TEXT_MUTED);
    } else {
        if (disks_selected_idx >= discovered_disk_count) disks_selected_idx = 0;
        const struct storage_disk_desc *d = &discovered_disks[disks_selected_idx];

        /* Header */
        gfx_draw_string(&surf, 204, 42, "DEVICE INSPECTION:", GFX_COLOR_TEXT_MUTED);
        char fulldev[32];
        fulldev[0] = '/'; fulldev[1] = 'd'; fulldev[2] = 'e'; fulldev[3] = 'v'; fulldev[4] = '/';
        str_copy(&fulldev[5], d->name, 26);
        gfx_draw_string(&surf, 348, 42, fulldev, GFX_COLOR_EMERALD_ACCENT);

        /* Metadata rows */
        gfx_draw_string(&surf, 204, 60, "Model:", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 280, 60, d->model, GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, 204, 76, "Type:", GFX_COLOR_TEXT_SECONDARY);
        const char *type_name = (d->type == BLOCK_DEV_TYPE_SATA) ? "SATA Mass Storage" :
                                (d->type == BLOCK_DEV_TYPE_NVME) ? "NVMe Solid State Drive" :
                                (d->type == BLOCK_DEV_TYPE_VIRTIO) ? "VirtIO Block Device" :
                                (d->type == BLOCK_DEV_TYPE_OPTICAL) ? "Optical Disc (CD/DVD)" : "Block Device";
        gfx_draw_string(&surf, 280, 76, type_name, GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, 204, 92, "Scheme:", GFX_COLOR_TEXT_SECONDARY);
        const char *scheme_name = (d->scheme == PARTITION_SCHEME_GPT) ? "GPT (GUID Partition Table)" :
                                  (d->scheme == PARTITION_SCHEME_MBR) ? "MBR (Master Boot Record)" :
                                  "RAW / Direct Filesystem";
        gfx_draw_string(&surf, 280, 92, scheme_name, GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, 204, 108, "Capacity:", GFX_COLOR_TEXT_SECONDARY);
        char dcap[32];
        format_disk_capacity(d->capacity_bytes, dcap, sizeof(dcap));
        gfx_draw_string(&surf, 280, 108, dcap, GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, 204, 124, "Sectors:", GFX_COLOR_TEXT_SECONDARY);
        char sec_buf[32];
        u64_to_buf(d->total_blocks, sec_buf, sizeof(sec_buf));
        gfx_draw_string(&surf, 280, 124, sec_buf, GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, 204, 140, "Status:", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 280, 140, "[READ-ONLY PROTECTION ACTIVE]", GFX_COLOR_EMERALD_HIGHLIGHT);

        /* Divider */
        gfx_draw_line(&surf, 198, 158, 568, 158, GFX_COLOR_GLASS_BORDER);

        /* Partition Slices Table */
        char phdr[32];
        str_copy(phdr, "PARTITIONS (", sizeof(phdr));
        char pcnt[16];
        u64_to_buf((uint64_t)d->partition_count, pcnt, sizeof(pcnt));
        size_t plen = str_len(phdr);
        for (size_t c = 0; pcnt[c] != '\0' && plen + 2 < sizeof(phdr); c++) phdr[plen++] = pcnt[c];
        phdr[plen++] = ')'; phdr[plen] = '\0';
        gfx_draw_string(&surf, 204, 164, phdr, GFX_COLOR_TEXT_PRIMARY);

        /* Table header row */
        gfx_draw_rect_filled(&surf, 198, 180, 370, 18, GFX_COLOR_EMERALD_PRIMARY);
        gfx_draw_string(&surf, 204, 182, "SLICE", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 276, 182, "START LBA", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 360, 182, "SECTORS", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 436, 182, "FS", GFX_COLOR_TEXT_SECONDARY);
        gfx_draw_string(&surf, 492, 182, "MOUNT", GFX_COLOR_TEXT_SECONDARY);

        if (d->partition_count == 0) {
            gfx_draw_string(&surf, 204, 204, "(No partition slices defined on device)", GFX_COLOR_TEXT_MUTED);
        } else {
            for (uint32_t p = 0; p < d->partition_count && p < STORAGE_MAX_PARTITIONS_PER_DISK; p++) {
                uint32_t py = 202U + p * 18U;
                const struct storage_partition_desc *sp = &d->partitions[p];

                if (p % 2 == 1) {
                    gfx_draw_rect_filled(&surf, 198, py - 2, 370, 18, 0x300E3D2AU);
                }
                gfx_draw_string(&surf, 204, py, sp->slice_name, GFX_COLOR_TEXT_PRIMARY);

                char lba_str[24];
                u64_to_buf(sp->start_lba, lba_str, sizeof(lba_str));
                gfx_draw_string(&surf, 276, py, lba_str, GFX_COLOR_TEXT_MUTED);

                char scnt_str[24];
                u64_to_buf(sp->sector_count, scnt_str, sizeof(scnt_str));
                gfx_draw_string(&surf, 360, py, scnt_str, GFX_COLOR_TEXT_MUTED);

                gfx_draw_string(&surf, 436, py, sp->fs_name[0] ? sp->fs_name : "RAW", GFX_COLOR_EMERALD_ACCENT);

                const char *mnt = sp->mounted ? sp->mount_point : "[Unmounted]";
                gfx_draw_string(&surf, 492, py, mnt, sp->mounted ? GFX_COLOR_SUCCESS : GFX_COLOR_DISABLED);
            }
        }
    }
}

void disks_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x < 0 || local_x >= (int32_t)DISKS_WIDTH || local_y < 0 || local_y >= (int32_t)DISKS_HEIGHT) return;

    /* Left pane disk selection (y in [58..320]) */
    if (local_x >= 10 && local_x <= 182 && local_y >= 58 && local_y <= 320) {
        size_t idx = (size_t)(local_y - 58) / 44U;
        if (idx < discovered_disk_count) {
            disks_selected_idx = idx;
            disks_window_render();
            if (disks_win != NULL) wm_mark_window_damaged(disks_win);
        }
    }
}

static void disks_on_window_close(struct window *win) {
    (void)win;
    disks_win = NULL;
}

struct window *disks_window_open(void) {
    if (disks_win != NULL) {
        if (disks_win->minimized) wm_minimize_window(disks_win->id);
        wm_focus_window(disks_win->id);
        return disks_win;
    }

    if (!storage_mgr_initialized) storage_manager_initialize();

    disks_win = wm_create_window("Disks", 160, 100, DISKS_WIDTH, DISKS_HEIGHT,
                                 WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR | WM_FLAG_RESIZABLE);
    if (disks_win != NULL) {
        disks_win->client_buffer = disks_render_buf;
        disks_win->client_pitch = DISKS_WIDTH * sizeof(uint32_t);
        disks_win->client_width = DISKS_WIDTH;
        disks_win->client_height = DISKS_HEIGHT;
        disks_win->on_click = disks_handle_click;
        disks_win->on_close = disks_on_window_close;
        disks_window_render();
        wm_mark_window_damaged(disks_win);
    }
    return disks_win;
}

/* Self-test */
static uint8_t test_mgr_disk_storage[512 * 8192]; /* 4MB RAM disk */

bool storage_manager_self_test(void) {
    if (!storage_manager_initialize()) return false;
    for (size_t i = 0; i < sizeof(test_mgr_disk_storage); i++) {
        test_mgr_disk_storage[i] = 0;
    }

    struct block_device *disk = ramdisk_create("test_sdisk", 512, 8192, test_mgr_disk_storage);
    if (disk == NULL) return false;
    disk->type = BLOCK_DEV_TYPE_SATA;

    /* Add disk to discovered list for test */
    size_t prev_count = discovered_disk_count;
    if (discovered_disk_count < STORAGE_MAX_DISKS) {
        struct storage_disk_desc *desc = &discovered_disks[discovered_disk_count++];
        *desc = (struct storage_disk_desc){0};
        str_copy(desc->name, "test_sdisk", BLOCK_DEV_NAME_MAX);
        str_copy(desc->model, "DAWN-OS SATA Drive", sizeof(desc->model));
        desc->type = BLOCK_DEV_TYPE_SATA;
        desc->block_size = 512;
        desc->total_blocks = 8192;
        desc->capacity_bytes = 4194304;

        /* Create partition on test_sdisk */
        char slice_name[BLOCK_DEV_NAME_MAX] = {0};
        int res = storage_create_partition("test_sdisk", 2048, MBR_TYPE_FAT32_LBA, slice_name);
        if (res != STORAGE_OK) return false;
        if (!str_equal(slice_name, "test_sdisk1")) return false;

        /* Query disk info */
        const struct storage_disk_desc *qdisk = storage_find_disk("test_sdisk");
        if (qdisk == NULL || qdisk->partition_count != 1) return false;
        if (!str_equal(qdisk->partitions[0].slice_name, "test_sdisk1")) return false;
        if (qdisk->partitions[0].capacity_bytes != 1048576) return false;

        /* Sector bounds safety test: out-of-range sector request must return STORAGE_ERR_BOUNDS */
        char dummy_slice[BLOCK_DEV_NAME_MAX] = {0};
        if (storage_create_partition("test_sdisk", 100000ULL, MBR_TYPE_FAT32_LBA, dummy_slice) != STORAGE_ERR_BOUNDS) {
            return false;
        }

        /* Storage Safety Gate: Read-only protection enforcement */
        disk->read_only = true;
        desc->read_only = true;
        if (storage_create_partition("test_sdisk", 1024, MBR_TYPE_FAT32_LBA, dummy_slice) != STORAGE_ERR_READ_ONLY) {
            return false;
        }
        if (storage_delete_partition("test_sdisk", 1) != STORAGE_ERR_READ_ONLY) {
            return false;
        }
        if (storage_format_partition("test_sdisk1", "fat32", "TEST") != STORAGE_ERR_READ_ONLY) {
            return false;
        }
        disk->read_only = false;
        desc->read_only = false;

        /* Delete partition */
        if (storage_delete_partition("test_sdisk", 1) != STORAGE_OK) return false;
        if (qdisk->partition_count != 0) return false;

        /* Restore discovered count */
        discovered_disk_count = prev_count;
    }

    block_dev_unregister(disk->id);
    return true;
}

bool disks_gui_self_test(void) {
    /* Disks GUI Window integration test */
    struct window *dwin = disks_window_open();
    if (dwin == NULL) return false;
    if (dwin->client_buffer != disks_render_buf) return false;
    if (dwin->client_width != DISKS_WIDTH || dwin->client_height != DISKS_HEIGHT) return false;

    /* Simulate mouse click selection */
    disks_handle_click(20, 70);

    /* Close Disks window */
    if (wm_destroy_window(dwin->id) != 0) return false;
    if (disks_win != NULL) return false;

    return true;
}
