/* SPDX-License-Identifier: Unlicense */
#include "block_dev.h"

static struct block_device device_registry[BLOCK_DEV_MAX_DEVICES];
static uint32_t next_dev_id = 1;
static bool initialized = false;

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

bool block_dev_initialize(void) {
    if (initialized) return true;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        device_registry[i] = (struct block_device){0};
    }
    next_dev_id = 1;
    initialized = true;
    return true;
}

int block_dev_register(struct block_device *dev) {
    if (!initialized) block_dev_initialize();
    if (dev == NULL || dev->block_size == 0 || dev->total_blocks == 0) return -1;

    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (!device_registry[i].active) {
            device_registry[i] = *dev;
            device_registry[i].id = next_dev_id++;
            device_registry[i].active = true;
            device_registry[i].capacity_bytes = dev->total_blocks * (uint64_t)dev->block_size;
            if (dev->name[0] != '\0') {
                str_copy(device_registry[i].name, dev->name, BLOCK_DEV_NAME_MAX);
            }
            dev->id = device_registry[i].id;
            return (int)device_registry[i].id;
        }
    }
    return -1;
}

int block_dev_unregister(uint32_t id) {
    if (id == 0) return -1;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (device_registry[i].active && device_registry[i].id == id) {
            device_registry[i].active = false;
            return 0;
        }
    }
    return -1;
}

struct block_device *block_dev_lookup(const char *name) {
    if (name == NULL) return NULL;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (device_registry[i].active && str_equal(device_registry[i].name, name)) {
            return &device_registry[i];
        }
    }
    return NULL;
}

struct block_device *block_dev_get_by_id(uint32_t id) {
    if (id == 0) return NULL;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (device_registry[i].active && device_registry[i].id == id) {
            return &device_registry[i];
        }
    }
    return NULL;
}

struct block_device *block_dev_get_by_index(size_t index) {
    size_t count = 0;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (device_registry[i].active) {
            if (count == index) {
                return &device_registry[i];
            }
            count++;
        }
    }
    return NULL;
}

size_t block_dev_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < BLOCK_DEV_MAX_DEVICES; i++) {
        if (device_registry[i].active) {
            count++;
        }
    }
    return count;
}

int block_dev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || !dev->active || buffer == NULL || count == 0) return -1;
    if (lba >= dev->total_blocks || (uint64_t)count > dev->total_blocks - lba) return -1;
    if (dev->ops.read_blocks == NULL) return -1;
    return dev->ops.read_blocks(dev, lba, count, buffer);
}

int block_dev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || !dev->active || buffer == NULL || count == 0) return -1;
    if (dev->read_only) return -1;
    if (lba >= dev->total_blocks || (uint64_t)count > dev->total_blocks - lba) return -1;
    if (dev->ops.write_blocks == NULL) return -1;
    return dev->ops.write_blocks(dev, lba, count, buffer);
}

/* Ramdisk driver */
static int ramdisk_read_blocks(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || dev->driver_data == NULL || buffer == NULL) return -1;
    uint8_t *storage = (uint8_t *)dev->driver_data;
    uint8_t *dest = (uint8_t *)buffer;
    uint64_t offset = lba * (uint64_t)dev->block_size;
    uint64_t bytes = (uint64_t)count * (uint64_t)dev->block_size;

    for (uint64_t i = 0; i < bytes; i++) {
        dest[i] = storage[offset + i];
    }
    return 0;
}

static int ramdisk_write_blocks(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || dev->driver_data == NULL || buffer == NULL) return -1;
    uint8_t *storage = (uint8_t *)dev->driver_data;
    const uint8_t *src = (const uint8_t *)buffer;
    uint64_t offset = lba * (uint64_t)dev->block_size;
    uint64_t bytes = (uint64_t)count * (uint64_t)dev->block_size;

    for (uint64_t i = 0; i < bytes; i++) {
        storage[offset + i] = src[i];
    }
    return 0;
}

static int ramdisk_flush(struct block_device *dev) {
    (void)dev;
    return 0;
}

struct block_device *ramdisk_create(const char *name, uint32_t block_size, uint64_t total_blocks, void *storage_buffer) {
    if (storage_buffer == NULL || block_size == 0 || total_blocks == 0) return NULL;
    struct block_device dev = {0};
    str_copy(dev.name, name ? name : "ramdisk", BLOCK_DEV_NAME_MAX);
    dev.type = BLOCK_DEV_TYPE_RAMDISK;
    dev.block_size = block_size;
    dev.total_blocks = total_blocks;
    dev.read_only = false;
    dev.driver_data = storage_buffer;
    dev.ops.read_blocks = ramdisk_read_blocks;
    dev.ops.write_blocks = ramdisk_write_blocks;
    dev.ops.flush = ramdisk_flush;

    int id = block_dev_register(&dev);
    if (id < 0) return NULL;
    return block_dev_get_by_id((uint32_t)id);
}

/* Self-test */
static uint8_t test_ramdisk_storage[512 * 8];

bool block_dev_self_test(void) {
    if (!block_dev_initialize()) return false;
    for (size_t i = 0; i < sizeof(test_ramdisk_storage); i++) {
        test_ramdisk_storage[i] = 0;
    }

    struct block_device *rd = ramdisk_create("test_rd0", 512, 8, test_ramdisk_storage);
    if (rd == NULL) return false;
    if (!str_equal(rd->name, "test_rd0")) return false;
    if (rd->total_blocks != 8 || rd->block_size != 512) return false;
    if (rd->capacity_bytes != 4096) return false;

    /* Write data pattern */
    uint8_t write_buf[512];
    for (size_t i = 0; i < 512; i++) write_buf[i] = (uint8_t)(i & 0xFFU);
    if (block_dev_write(rd, 2, 1, write_buf) != 0) return false;

    /* Read back and verify */
    uint8_t read_buf[512] = {0};
    if (block_dev_read(rd, 2, 1, read_buf) != 0) return false;
    for (size_t i = 0; i < 512; i++) {
        if (read_buf[i] != (uint8_t)(i & 0xFFU)) return false;
    }

    /* Out of bounds write test */
    if (block_dev_write(rd, 8, 1, write_buf) == 0) return false;
    if (block_dev_write(rd, 7, 2, write_buf) == 0) return false;

    /* Read-only verification */
    rd->read_only = true;
    if (block_dev_write(rd, 2, 1, write_buf) == 0) return false;
    rd->read_only = false;

    /* Lookup tests */
    if (block_dev_lookup("test_rd0") != rd) return false;
    if (block_dev_get_by_id(rd->id) != rd) return false;

    /* Cleanup */
    if (block_dev_unregister(rd->id) != 0) return false;
    if (block_dev_lookup("test_rd0") != NULL) return false;

    return true;
}
