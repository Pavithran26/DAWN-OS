/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_BLOCK_DEV_H
#define DAWN_BLOCK_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCK_DEV_NAME_MAX 32U
#define BLOCK_DEV_MAX_DEVICES 32U

enum block_dev_type {
    BLOCK_DEV_TYPE_UNKNOWN = 0,
    BLOCK_DEV_TYPE_SATA = 1,
    BLOCK_DEV_TYPE_NVME = 2,
    BLOCK_DEV_TYPE_PARTITION = 3,
    BLOCK_DEV_TYPE_RAMDISK = 4,
    BLOCK_DEV_TYPE_OPTICAL = 5
};

struct block_device;

struct block_device_ops {
    int (*read_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
    int (*write_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
    int (*flush)(struct block_device *dev);
};

struct block_device {
    char name[BLOCK_DEV_NAME_MAX];
    uint32_t id;
    enum block_dev_type type;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t capacity_bytes;
    bool read_only;
    bool active;
    struct block_device_ops ops;
    void *driver_data;
};

bool block_dev_initialize(void);
int block_dev_register(struct block_device *dev);
int block_dev_unregister(uint32_t id);
struct block_device *block_dev_lookup(const char *name);
struct block_device *block_dev_get_by_id(uint32_t id);
struct block_device *block_dev_get_by_index(size_t index);
size_t block_dev_count(void);
int block_dev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
int block_dev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
struct block_device *ramdisk_create(const char *name, uint32_t block_size, uint64_t total_blocks, void *storage_buffer);

bool block_dev_self_test(void);

#endif /* DAWN_BLOCK_DEV_H */
