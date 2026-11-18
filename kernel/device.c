/* SPDX-License-Identifier: Unlicense */
#include "device.h"

static struct device device_table[DEVICE_MAX_COUNT];
static struct driver driver_table[DRIVER_MAX_COUNT];
static uint32_t next_device_id = 1;
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

bool device_manager_initialize(void) {
    if (initialized) return true;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        device_table[i] = (struct device){0};
    }
    for (size_t i = 0; i < DRIVER_MAX_COUNT; i++) {
        driver_table[i] = (struct driver){0};
    }
    next_device_id = 1;
    initialized = true;
    return true;
}

int device_register(struct device *dev) {
    if (!initialized) device_manager_initialize();
    if (dev == NULL) return -1;

    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (!device_table[i].active) {
            device_table[i] = *dev;
            device_table[i].id = next_device_id++;
            device_table[i].active = true;
            if (dev->status == DEV_STATUS_UNINITIALIZED) {
                device_table[i].status = DEV_STATUS_RUNNING;
            }
            dev->id = device_table[i].id;
            return (int)device_table[i].id;
        }
    }
    return -1;
}

int device_unregister(uint32_t device_id) {
    if (device_id == 0) return -1;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (device_table[i].active && device_table[i].id == device_id) {
            if (device_table[i].driver && device_table[i].driver->remove) {
                device_table[i].driver->remove(&device_table[i]);
            }
            device_table[i].active = false;
            return 0;
        }
    }
    return -1;
}

struct device *device_get_by_id(uint32_t device_id) {
    if (device_id == 0) return NULL;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (device_table[i].active && device_table[i].id == device_id) {
            return &device_table[i];
        }
    }
    return NULL;
}

struct device *device_lookup(const char *name) {
    if (name == NULL) return NULL;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (device_table[i].active && str_equal(device_table[i].name, name)) {
            return &device_table[i];
        }
    }
    return NULL;
}

struct device *device_get_by_index(size_t index) {
    size_t count = 0;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (device_table[i].active) {
            if (count == index) {
                return &device_table[i];
            }
            count++;
        }
    }
    return NULL;
}

size_t device_get_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        if (device_table[i].active) count++;
    }
    return count;
}

int driver_register(struct driver *drv) {
    if (!initialized) device_manager_initialize();
    if (drv == NULL || drv->name[0] == '\0') return -1;

    for (size_t i = 0; i < DRIVER_MAX_COUNT; i++) {
        if (!driver_table[i].active) {
            driver_table[i] = *drv;
            driver_table[i].active = true;
            return 0;
        }
    }
    return -1;
}

int driver_unregister(const char *name) {
    if (name == NULL) return -1;
    for (size_t i = 0; i < DRIVER_MAX_COUNT; i++) {
        if (driver_table[i].active && str_equal(driver_table[i].name, name)) {
            driver_table[i].active = false;
            return 0;
        }
    }
    return -1;
}

struct driver *driver_lookup(const char *name) {
    if (name == NULL) return NULL;
    for (size_t i = 0; i < DRIVER_MAX_COUNT; i++) {
        if (driver_table[i].active && str_equal(driver_table[i].name, name)) {
            return &driver_table[i];
        }
    }
    return NULL;
}

int driver_bind_all(void) {
    int bound = 0;
    for (size_t d = 0; d < DEVICE_MAX_COUNT; d++) {
        if (device_table[d].active && device_table[d].driver == NULL) {
            for (size_t r = 0; r < DRIVER_MAX_COUNT; r++) {
                if (driver_table[r].active) {
                    if (driver_table[r].bus == device_table[d].bus ||
                        driver_table[r].class_type == device_table[d].class_type) {
                        if (driver_table[r].probe && driver_table[r].probe(&device_table[d])) {
                            device_table[d].driver = &driver_table[r];
                            if (driver_table[r].init) {
                                driver_table[r].init(&device_table[d]);
                            }
                            device_table[d].status = DEV_STATUS_RUNNING;
                            bound++;
                            break;
                        }
                    }
                }
            }
        }
    }
    return bound;
}

/* Self-test */
static bool test_probe_called = false;
static bool test_init_called = false;
static bool test_remove_called = false;

static bool test_driver_probe(struct device *dev) {
    (void)dev;
    test_probe_called = true;
    return true;
}

static int test_driver_init(struct device *dev) {
    (void)dev;
    test_init_called = true;
    return 0;
}

static int test_driver_remove(struct device *dev) {
    (void)dev;
    test_remove_called = true;
    return 0;
}

bool device_manager_self_test(void) {
    if (!device_manager_initialize()) return false;

    struct driver drv = {0};
    str_copy(drv.name, "test_drv", DRIVER_NAME_MAX);
    drv.bus = BUS_TYPE_PLATFORM;
    drv.class_type = DEV_CLASS_SYSTEM;
    drv.probe = test_driver_probe;
    drv.init = test_driver_init;
    drv.remove = test_driver_remove;

    if (driver_register(&drv) != 0) return false;
    if (driver_lookup("test_drv") == NULL) return false;

    struct device dev = {0};
    str_copy(dev.name, "test_dev0", DEVICE_NAME_MAX);
    dev.bus = BUS_TYPE_PLATFORM;
    dev.class_type = DEV_CLASS_SYSTEM;
    dev.status = DEV_STATUS_UNINITIALIZED;

    int dev_id = device_register(&dev);
    if (dev_id < 0) return false;

    struct device *lookup = device_lookup("test_dev0");
    if (lookup == NULL || lookup->id != (uint32_t)dev_id) return false;

    /* Bind driver to device */
    test_probe_called = false;
    test_init_called = false;
    int bound = driver_bind_all();
    if (bound < 1 || !test_probe_called || !test_init_called) return false;
    if (lookup->driver == NULL || !str_equal(lookup->driver->name, "test_drv")) return false;
    if (lookup->status != DEV_STATUS_RUNNING) return false;

    /* Unregister device */
    test_remove_called = false;
    if (device_unregister((uint32_t)dev_id) != 0) return false;
    if (!test_remove_called) return false;
    if (device_lookup("test_dev0") != NULL) return false;

    /* Unregister driver */
    if (driver_unregister("test_drv") != 0) return false;
    if (driver_lookup("test_drv") != NULL) return false;

    return true;
}
