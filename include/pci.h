/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PCI_H
#define DAWN_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0CF8U
#define PCI_CONFIG_DATA    0x0CFCU

#define PCI_CLASS_MASS_STORAGE 0x01U
#define PCI_CLASS_NETWORK      0x02U
#define PCI_CLASS_DISPLAY      0x03U
#define PCI_CLASS_MULTIMEDIA   0x04U
#define PCI_CLASS_BRIDGE       0x06U
#define PCI_CLASS_SERIAL_BUS   0x0CU

/* Mass Storage subclasses */
#define PCI_SUBCLASS_STORAGE_IDE  0x01U
#define PCI_SUBCLASS_STORAGE_SATA 0x06U
#define PCI_SUBCLASS_STORAGE_NVME 0x08U

/* Programming interfaces */
#define PCI_PROGIF_SATA_AHCI 0x01U
#define PCI_PROGIF_NVME      0x02U

/* Serial Bus subclasses */
#define PCI_SUBCLASS_SERIAL_USB 0x03U
#define PCI_PROGIF_USB_XHCI     0x30U

#define PCI_MAX_DEVICES 64U

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint32_t bar[6];
    uint8_t irq_line;
};

void pci_initialize(void);
uint32_t pci_read_config_u32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config_u16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_read_config_u8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_u32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write_config_u16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

size_t pci_scan_all(void);
size_t pci_get_device_count(void);
const struct pci_device *pci_get_device(size_t index);
const struct pci_device *pci_find_device(uint16_t vendor_id, uint16_t device_id);
size_t pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_device *out_devices, size_t max_count);

/* Storage helper */
size_t pci_discover_storage_controllers(struct pci_device *out_devices, size_t max_count);

bool pci_self_test(void);

#endif /* DAWN_PCI_H */
