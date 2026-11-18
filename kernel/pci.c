/* SPDX-License-Identifier: Unlicense */
#include "pci.h"

static struct pci_device device_table[PCI_MAX_DEVICES];
static size_t device_count = 0;
static bool pci_scanned = false;

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)((1U << 31) |
                      (((uint32_t)bus) << 16) |
                      (((uint32_t)(slot & 0x1FU)) << 11) |
                      (((uint32_t)(func & 0x07U)) << 8) |
                      (((uint32_t)offset) & 0xFCU));
}

uint32_t pci_read_config_u32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config_u16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t val = inl(PCI_CONFIG_DATA);
    uint32_t shift = (uint32_t)((offset & 2U) * 8U);
    return (uint16_t)((val >> shift) & 0xFFFFU);
}

uint8_t pci_read_config_u8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t val = inl(PCI_CONFIG_DATA);
    uint32_t shift = (uint32_t)((offset & 3U) * 8U);
    return (uint8_t)((val >> shift) & 0xFFU);
}

void pci_write_config_u32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write_config_u16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t current = pci_read_config_u32(bus, slot, func, (uint8_t)(offset & 0xFCU));
    uint32_t shift = (uint32_t)((offset & 2U) * 8U);
    uint32_t mask = ~(0xFFFFU << shift);
    current = (current & mask) | (((uint32_t)value) << shift);
    pci_write_config_u32(bus, slot, func, (uint8_t)(offset & 0xFCU), current);
}

void pci_initialize(void) {
    if (pci_scanned) return;
    pci_scan_all();
}

static void probe_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_read_config_u16(bus, slot, func, 0x00);
    if (vendor_id == 0xFFFFU || vendor_id == 0x0000U) return;

    if (device_count >= PCI_MAX_DEVICES) return;

    struct pci_device *dev = &device_table[device_count];
    *dev = (struct pci_device){0};
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config_u16(bus, slot, func, 0x02);

    uint32_t class_reg = pci_read_config_u32(bus, slot, func, 0x08);
    dev->revision = (uint8_t)(class_reg & 0xFFU);
    dev->prog_if = (uint8_t)((class_reg >> 8) & 0xFFU);
    dev->subclass = (uint8_t)((class_reg >> 16) & 0xFFU);
    dev->class_code = (uint8_t)((class_reg >> 24) & 0xFFU);

    uint8_t header = pci_read_config_u8(bus, slot, func, 0x0E);
    dev->header_type = header;

    /* Read BARs for Type 0 header */
    if ((header & 0x7FU) == 0x00U) {
        for (uint8_t i = 0; i < 6; i++) {
            dev->bar[i] = pci_read_config_u32(bus, slot, func, (uint8_t)(0x10U + i * 4U));
        }
    }

    dev->irq_line = pci_read_config_u8(bus, slot, func, 0x3C);
    device_count++;
}

size_t pci_scan_all(void) {
    device_count = 0;
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read_config_u16((uint8_t)bus, slot, 0, 0x00);
            if (vendor == 0xFFFFU || vendor == 0x0000U) continue;

            probe_function((uint8_t)bus, slot, 0);

            uint8_t header = pci_read_config_u8((uint8_t)bus, slot, 0, 0x0E);
            if ((header & 0x80U) != 0) {
                /* Multi-function device */
                for (uint8_t func = 1; func < 8; func++) {
                    uint16_t fn_vendor = pci_read_config_u16((uint8_t)bus, slot, func, 0x00);
                    if (fn_vendor != 0xFFFFU && fn_vendor != 0x0000U) {
                        probe_function((uint8_t)bus, slot, func);
                    }
                }
            }
        }
    }
    pci_scanned = true;
    return device_count;
}

size_t pci_get_device_count(void) {
    if (!pci_scanned) pci_scan_all();
    return device_count;
}

const struct pci_device *pci_get_device(size_t index) {
    if (!pci_scanned) pci_scan_all();
    if (index >= device_count) return NULL;
    return &device_table[index];
}

const struct pci_device *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    if (!pci_scanned) pci_scan_all();
    for (size_t i = 0; i < device_count; i++) {
        if (device_table[i].vendor_id == vendor_id && device_table[i].device_id == device_id) {
            return &device_table[i];
        }
    }
    return NULL;
}

size_t pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_device *out_devices, size_t max_count) {
    if (!pci_scanned) pci_scan_all();
    if (out_devices == NULL || max_count == 0) return 0;
    size_t found = 0;
    for (size_t i = 0; i < device_count && found < max_count; i++) {
        if (device_table[i].class_code == class_code) {
            if (subclass == 0xFFU || device_table[i].subclass == subclass) {
                if (prog_if == 0xFFU || device_table[i].prog_if == prog_if) {
                    out_devices[found++] = device_table[i];
                }
            }
        }
    }
    return found;
}

size_t pci_discover_storage_controllers(struct pci_device *out_devices, size_t max_count) {
    return pci_find_by_class(PCI_CLASS_MASS_STORAGE, 0xFFU, 0xFFU, out_devices, max_count);
}

/* Self-test */
bool pci_self_test(void) {
    /* Test PCI address calculation */
    uint32_t addr = pci_make_address(0, 31, 2, 0x10);
    uint32_t expected = (1U << 31) | (0U << 16) | (31U << 11) | (2U << 8) | 0x10U;
    if (addr != expected) return false;

    addr = pci_make_address(1, 0, 0, 0x08);
    expected = (1U << 31) | (1U << 16) | (0U << 11) | (0U << 8) | 0x08U;
    if (addr != expected) return false;

    /* Verify device table mechanics */
    struct pci_device synth_dev = {
        .bus = 0,
        .slot = 1,
        .func = 0,
        .vendor_id = 0x8086,
        .device_id = 0x2922,
        .class_code = PCI_CLASS_MASS_STORAGE,
        .subclass = PCI_SUBCLASS_STORAGE_SATA,
        .prog_if = PCI_PROGIF_SATA_AHCI,
        .revision = 0x02,
        .header_type = 0x00,
        .bar = {0, 0, 0, 0, 0, 0xFEBD0000},
        .irq_line = 11
    };

    struct pci_device match_dev[4];
    size_t prev_count = device_count;
    if (device_count < PCI_MAX_DEVICES) {
        device_table[device_count++] = synth_dev;
        pci_scanned = true;

        size_t n = pci_find_by_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_STORAGE_SATA, PCI_PROGIF_SATA_AHCI, match_dev, 4);
        if (n == 0) return false;
        if (match_dev[0].vendor_id != 0x8086 || match_dev[0].device_id != 0x2922) return false;

        /* Reset count back */
        device_count = prev_count;
    }

    return true;
}
