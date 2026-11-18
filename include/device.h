/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_DEVICE_H
#define DAWN_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_NAME_MAX 32U
#define DRIVER_NAME_MAX 32U
#define DEVICE_MAX_COUNT 64U
#define DRIVER_MAX_COUNT 32U

enum device_bus {
    BUS_TYPE_PLATFORM = 0,
    BUS_TYPE_PCI = 1,
    BUS_TYPE_USB = 2,
    BUS_TYPE_PS2 = 3
};

enum device_class {
    DEV_CLASS_UNKNOWN = 0,
    DEV_CLASS_SYSTEM = 1,
    DEV_CLASS_STORAGE = 2,
    DEV_CLASS_NETWORK = 3,
    DEV_CLASS_DISPLAY = 4,
    DEV_CLASS_INPUT = 5,
    DEV_CLASS_AUDIO = 6,
    DEV_CLASS_SERIAL = 7
};

enum device_status {
    DEV_STATUS_UNINITIALIZED = 0,
    DEV_STATUS_RUNNING = 1,
    DEV_STATUS_SUSPENDED = 2,
    DEV_STATUS_FAILED = 3
};

struct device;
struct driver;

struct device_resource {
    uint32_t io_port_base;
    uint32_t io_port_count;
    uint64_t mmio_base;
    uint64_t mmio_size;
    uint8_t  irq_line;
};

struct device {
    char name[DEVICE_NAME_MAX];
    uint32_t id;
    enum device_bus bus;
    enum device_class class_type;
    enum device_status status;
    struct device_resource resource;
    struct device *parent;
    struct driver *driver;
    void *driver_data;
    bool active;
};

struct driver {
    char name[DRIVER_NAME_MAX];
    enum device_bus bus;
    enum device_class class_type;
    bool (*probe)(struct device *dev);
    int (*init)(struct device *dev);
    int (*remove)(struct device *dev);
    int (*suspend)(struct device *dev);
    int (*resume)(struct device *dev);
    bool active;
};

bool device_manager_initialize(void);
int device_register(struct device *dev);
int device_unregister(uint32_t device_id);
struct device *device_get_by_id(uint32_t device_id);
struct device *device_lookup(const char *name);
struct device *device_get_by_index(size_t index);
size_t device_get_count(void);

int driver_register(struct driver *drv);
int driver_unregister(const char *name);
struct driver *driver_lookup(const char *name);
int driver_bind_all(void);

bool device_manager_self_test(void);

#endif /* DAWN_DEVICE_H */
