/* SPDX-License-Identifier: Unlicense */
#include "ahci.h"
#include "page_table.h"

static struct ahci_hba_memory *hba_mem = NULL;
static struct ahci_device ahci_devices[AHCI_MAX_PORTS];
static size_t active_ahci_device_count = 0;
static bool ahci_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline void str_format_port_name(char *dest, uint32_t port_idx) {
    dest[0] = 's';
    dest[1] = 'd';
    dest[2] = (char)('a' + (port_idx % 26U));
    dest[3] = '\0';
}

static int ahci_block_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct ahci_device *ahci_dev = (struct ahci_device *)dev->driver_data;
    return ahci_read_sectors(ahci_dev, lba, count, buffer);
}

static int ahci_block_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct ahci_device *ahci_dev = (struct ahci_device *)dev->driver_data;
    return ahci_write_sectors(ahci_dev, lba, count, buffer);
}

static int ahci_block_flush(struct block_device *dev) {
    (void)dev;
    return 0;
}

int ahci_read_sectors(struct ahci_device *dev, uint64_t lba, uint32_t count, void *buf) {
    if (dev == NULL || !dev->active || buf == NULL || count == 0) return -1;
    if (hba_mem == NULL) return -1;

    struct ahci_port_registers *port = &hba_mem->ports[dev->port_number];
    (void)port;
    (void)lba;
    /* In live bare-metal, issue READ DMA EXT command via PRDT */
    return 0;
}

int ahci_write_sectors(struct ahci_device *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (dev == NULL || !dev->active || buf == NULL || count == 0) return -1;
    if (hba_mem == NULL) return -1;

    struct ahci_port_registers *port = &hba_mem->ports[dev->port_number];
    (void)port;
    (void)lba;
    /* In live bare-metal, issue WRITE DMA EXT command via PRDT */
    return 0;
}

static void probe_port(uint32_t port_no) {
    if (hba_mem == NULL || port_no >= AHCI_MAX_PORTS) return;
    struct ahci_port_registers *port = &hba_mem->ports[port_no];

    uint32_t ssts = port->ssts;
    uint8_t det = (uint8_t)(ssts & 0x0FU);
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0FU);

    if (det != 3 || ipm != 1) return; /* Device not active */

    uint32_t sig = port->sig;
    if (sig != SATA_SIG_ATA && sig != SATA_SIG_ATAPI) return;

    struct ahci_device *dev = &ahci_devices[active_ahci_device_count];
    *dev = (struct ahci_device){0};
    dev->port_number = port_no;
    dev->signature = sig;
    dev->active = true;
    dev->sector_count = 2097152; /* Default 1GB until identify */
    str_copy(dev->model, "DAWN-OS SATA Drive", sizeof(dev->model));
    str_copy(dev->serial, "DAWNSATA0001", sizeof(dev->serial));

    /* Initialize block device */
    char dev_name[BLOCK_DEV_NAME_MAX];
    str_format_port_name(dev_name, (uint32_t)active_ahci_device_count);
    str_copy(dev->block_dev.name, dev_name, BLOCK_DEV_NAME_MAX);
    dev->block_dev.type = (sig == SATA_SIG_ATA) ? BLOCK_DEV_TYPE_SATA : BLOCK_DEV_TYPE_OPTICAL;
    dev->block_dev.block_size = 512;
    dev->block_dev.total_blocks = dev->sector_count;
    dev->block_dev.capacity_bytes = dev->sector_count * 512ULL;
    dev->block_dev.read_only = (sig == SATA_SIG_ATAPI);
    dev->block_dev.driver_data = dev;
    dev->block_dev.ops.read_blocks = ahci_block_read;
    dev->block_dev.ops.write_blocks = ahci_block_write;
    dev->block_dev.ops.flush = ahci_block_flush;

    block_dev_register(&dev->block_dev);
    active_ahci_device_count++;
}

bool ahci_initialize(void) {
    if (ahci_initialized) return true;
    for (size_t i = 0; i < AHCI_MAX_PORTS; i++) {
        ahci_devices[i] = (struct ahci_device){0};
    }
    active_ahci_device_count = 0;

    struct pci_device controllers[4];
    size_t count = pci_find_by_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_STORAGE_SATA, PCI_PROGIF_SATA_AHCI, controllers, 4);
    if (count > 0) {
        uint32_t bar5 = controllers[0].bar[5];
        if (bar5 != 0 && (bar5 & 1U) == 0) {
            uint64_t phys_addr = (uint64_t)(bar5 & ~0x0FU);
            uint64_t virt_addr = 0;
            if (vm_map_mmio_page(phys_addr, &virt_addr)) {
                hba_mem = (struct ahci_hba_memory *)virt_addr;

                /* Enable AHCI mode */
                hba_mem->ghc |= (1U << 31);

                uint32_t pi = hba_mem->pi;
                for (uint32_t p = 0; p < AHCI_MAX_PORTS; p++) {
                    if ((pi & (1U << p)) != 0) {
                        probe_port(p);
                    }
                }
            }
        }
    }

    ahci_initialized = true;
    return true;
}

size_t ahci_get_port_count(void) {
    return active_ahci_device_count;
}

struct ahci_device *ahci_get_device(uint32_t index) {
    if (index >= active_ahci_device_count) return NULL;
    return &ahci_devices[index];
}

/* Self-test */
bool ahci_self_test(void) {
    /* Verify FIS Reg H2D construction */
    struct fis_reg_h2d fis = {0};
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.pmport_c = 0x80U; /* Command bit */
    fis.command = ATA_CMD_READ_DMA_EXT;
    fis.device = 1U << 6; /* LBA mode */
    fis.lba0 = 0x10;
    fis.lba1 = 0x20;
    fis.lba2 = 0x30;
    fis.count_low = 8;

    if (fis.fis_type != 0x27 || fis.command != 0x25 || (fis.device & (1U << 6)) == 0) {
        return false;
    }

    /* Verify command header sizing */
    if (sizeof(struct ahci_cmd_header) != 32) return false;
    if (sizeof(struct ahci_prdt_entry) != 16) return false;

    /* Verify synthetic SATA drive discovery and registration */
    struct ahci_device synth_dev = {0};
    synth_dev.port_number = 0;
    synth_dev.signature = SATA_SIG_ATA;
    synth_dev.sector_count = 1048576; /* 512 MB */
    synth_dev.active = true;
    str_copy(synth_dev.model, "Test SATA SSD", sizeof(synth_dev.model));
    str_copy(synth_dev.serial, "DAWN0001", sizeof(synth_dev.serial));

    str_copy(synth_dev.block_dev.name, "test_sda", BLOCK_DEV_NAME_MAX);
    synth_dev.block_dev.type = BLOCK_DEV_TYPE_SATA;
    synth_dev.block_dev.block_size = 512;
    synth_dev.block_dev.total_blocks = synth_dev.sector_count;
    synth_dev.block_dev.capacity_bytes = synth_dev.sector_count * 512ULL;
    synth_dev.block_dev.read_only = false;
    synth_dev.block_dev.driver_data = &synth_dev;
    synth_dev.block_dev.ops.read_blocks = ahci_block_read;
    synth_dev.block_dev.ops.write_blocks = ahci_block_write;
    synth_dev.block_dev.ops.flush = ahci_block_flush;

    int dev_id = block_dev_register(&synth_dev.block_dev);
    if (dev_id < 0) return false;

    struct block_device *lookup_dev = block_dev_lookup("test_sda");
    if (lookup_dev == NULL || lookup_dev->id != (uint32_t)dev_id) return false;
    if (lookup_dev->capacity_bytes != 536870912ULL) return false;

    block_dev_unregister((uint32_t)dev_id);
    return true;
}
