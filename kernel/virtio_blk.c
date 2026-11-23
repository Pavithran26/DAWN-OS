/* SPDX-License-Identifier: Unlicense */
#include "virtio_blk.h"
#include "pci.h"
#include "physical_memory.h"
#include "limine_requests.h"

static struct virtio_blk_device vblk_dev = {0};
static struct block_device vblk_block_dev = {0};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void mem_zero(void *ptr, size_t len) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < len; i++) {
        p[i] = 0;
    }
}

static void mem_copy(void *dst, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
}

static void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

struct virtio_blk_device *virtio_blk_get_device(void) {
    return &vblk_dev;
}

static int virtio_blk_dev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    (void)dev;
    return virtio_blk_read_sectors(lba, count, buffer);
}

static int virtio_blk_dev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    (void)dev;
    return virtio_blk_write_sectors(lba, count, buffer);
}

static int virtio_blk_dev_flush(struct block_device *dev) {
    (void)dev;
    return 0;
}

bool virtio_blk_initialize(void) {
    if (vblk_dev.initialized) return true;

    pci_initialize();
    size_t dev_count = pci_get_device_count();
    const struct pci_device *target_pci = NULL;

    for (size_t i = 0; i < dev_count; i++) {
        const struct pci_device *pdev = pci_get_device(i);
        /* Accept ONLY legacy PCI device ID (0x1001). Modern devices (0x1042) require
         * modern MMIO/capability transport and are rejected by this legacy I/O driver. */
        if (pdev != NULL && pdev->vendor_id == VIRTIO_PCI_VENDOR_ID &&
            pdev->device_id == VIRTIO_BLK_PCI_DEVICE_ID) {
            target_pci = pdev;
            break;
        }
    }

    if (target_pci == NULL) {
        return false;
    }

    /* Enable Bus Master and I/O Space in PCI Command register */
    uint16_t cmd = pci_read_config_u16(target_pci->bus, target_pci->slot, target_pci->func, 0x04);
    cmd |= 0x0005U; /* I/O Space (bit 0) | Bus Master (bit 2) */
    pci_write_config_u16(target_pci->bus, target_pci->slot, target_pci->func, 0x04, cmd);

    /* Locate I/O BAR */
    uint16_t iobase = 0;
    for (uint8_t b = 0; b < 6; b++) {
        if ((target_pci->bar[b] & 1U) != 0U) {
            iobase = (uint16_t)(target_pci->bar[b] & ~0x3U);
            break;
        }
    }

    if (iobase == 0) {
        return false;
    }

    vblk_dev.iobase = iobase;

    /* 1. Reset device */
    outb(vblk_dev.iobase + VIRTIO_REG_DEVICE_STATUS, 0);

    /* 2. Set ACKNOWLEDGE */
    outb(vblk_dev.iobase + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* 3. Set DRIVER */
    outb(vblk_dev.iobase + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* 4. Feature negotiation: accept VIRTIO_BLK_F_RO if offered */
    uint32_t host_features = inl(vblk_dev.iobase + VIRTIO_REG_DEVICE_FEATURES);
    uint32_t guest_features = 0;
    if ((host_features & VIRTIO_BLK_F_RO) != 0U) {
        guest_features |= VIRTIO_BLK_F_RO;
        vblk_dev.read_only = true;
    } else {
        /* DAWN-OS read-only storage driver: enforce read-only at driver level */
        vblk_dev.read_only = true;
    }
    outl(vblk_dev.iobase + VIRTIO_REG_GUEST_FEATURES, guest_features);

    /* 5. Read capacity from device config (offset 0x14 in legacy VirtIO) */
    uint32_t cap_lo = inl((uint16_t)(vblk_dev.iobase + VIRTIO_REG_CONFIG_START));
    uint32_t cap_hi = inl((uint16_t)(vblk_dev.iobase + VIRTIO_REG_CONFIG_START + 4U));
    vblk_dev.capacity_sectors = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;

    if (limine_hhdm_request.response == NULL) {
        return false;
    }
    uint64_t hhdm = limine_hhdm_request.response->offset;

    /* 6. Setup VirtQueue 0 */
    outw(vblk_dev.iobase + VIRTIO_REG_QUEUE_SEL, 0);
    vblk_dev.qsize = inw(vblk_dev.iobase + VIRTIO_REG_QUEUE_SIZE);
    if (vblk_dev.qsize == 0) return false;

    size_t desc_size = (size_t)vblk_dev.qsize * sizeof(struct virtio_blk_vring_desc);
    size_t avail_size = sizeof(uint16_t) * (3U + (size_t)vblk_dev.qsize);
    size_t used_offset = (desc_size + avail_size + 4095U) & ~4095U;
    size_t used_size = sizeof(uint16_t) * 3U + (size_t)vblk_dev.qsize * sizeof(struct virtio_blk_vring_used_elem);
    size_t total = used_offset + used_size;
    size_t pages = (total + 4095U) / 4096U;

    if (!physical_allocator_allocate_contiguous(pages, &vblk_dev.vq_phys)) {
        return false;
    }
    uint8_t *vq_virt = (uint8_t *)(hhdm + vblk_dev.vq_phys);
    mem_zero(vq_virt, pages * 4096U);

    vblk_dev.desc = (struct virtio_blk_vring_desc *)(uintptr_t)vq_virt;
    vblk_dev.avail = (struct virtio_blk_vring_avail *)(uintptr_t)(vq_virt + desc_size);
    vblk_dev.used = (struct virtio_blk_vring_used *)(uintptr_t)(vq_virt + used_offset);
    vblk_dev.last_used_idx = 0;

    outl(vblk_dev.iobase + VIRTIO_REG_QUEUE_PFN, (uint32_t)(vblk_dev.vq_phys >> 12));

    /* 7. Allocate request header and status page (1 page) */
    if (!physical_allocator_allocate_contiguous(1, &vblk_dev.req_phys)) {
        return false;
    }
    uint8_t *req_virt = (uint8_t *)(hhdm + vblk_dev.req_phys);
    mem_zero(req_virt, 4096U);
    vblk_dev.req_hdr = (struct virtio_blk_req_hdr *)(uintptr_t)req_virt;
    vblk_dev.req_status = (volatile uint8_t *)(req_virt + 2048U);

    /* 8. Allocate DMA bounce buffer (16 pages = 64 KiB = 128 sectors) */
    if (!physical_allocator_allocate_contiguous(16, &vblk_dev.bounce_phys)) {
        return false;
    }
    vblk_dev.bounce_virt = (void *)(hhdm + vblk_dev.bounce_phys);
    mem_zero(vblk_dev.bounce_virt, 16U * 4096U);

    /* 9. Set DRIVER_OK */
    uint8_t status = inb(vblk_dev.iobase + VIRTIO_REG_DEVICE_STATUS);
    outb(vblk_dev.iobase + VIRTIO_REG_DEVICE_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER_OK));

    vblk_dev.initialized = true;

    /* 10. Register with block device manager */
    vblk_block_dev = (struct block_device){0};
    str_copy(vblk_block_dev.name, "vda", BLOCK_DEV_NAME_MAX);
    vblk_block_dev.type = BLOCK_DEV_TYPE_VIRTIO;
    vblk_block_dev.block_size = VIRTIO_BLK_SECTOR_SIZE;
    vblk_block_dev.total_blocks = vblk_dev.capacity_sectors;
    vblk_block_dev.capacity_bytes = vblk_dev.capacity_sectors * VIRTIO_BLK_SECTOR_SIZE;
    vblk_block_dev.read_only = true;
    vblk_block_dev.ops.read_blocks = virtio_blk_dev_read;
    vblk_block_dev.ops.write_blocks = virtio_blk_dev_write;
    vblk_block_dev.ops.flush = virtio_blk_dev_flush;
    vblk_block_dev.driver_data = &vblk_dev;

    (void)block_dev_register(&vblk_block_dev);

    return true;
}

int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buf) {
    if (!vblk_dev.initialized || buf == NULL || count == 0U) {
        return -1;
    }

    /* Range and overflow check against disk capacity */
    if (sector >= vblk_dev.capacity_sectors ||
        count > vblk_dev.capacity_sectors ||
        sector + count > vblk_dev.capacity_sectors ||
        sector + count < sector) {
        return -1;
    }

    uint8_t *dst = (uint8_t *)buf;
    uint64_t cur_sector = sector;
    uint32_t remaining = count;

    while (remaining > 0U) {
        uint32_t chunk = (remaining > VIRTIO_BLK_MAX_SECTORS) ? VIRTIO_BLK_MAX_SECTORS : remaining;
        uint32_t byte_count = chunk * VIRTIO_BLK_SECTOR_SIZE;

        /* Populate request header */
        vblk_dev.req_hdr->type = VIRTIO_BLK_T_IN;
        vblk_dev.req_hdr->reserved = 0;
        vblk_dev.req_hdr->sector = cur_sector;
        *vblk_dev.req_status = 0xFFU;

        /* Descriptor 0: Request Header (Device Reads) */
        vblk_dev.desc[0].addr = vblk_dev.req_phys;
        vblk_dev.desc[0].len = (uint32_t)sizeof(struct virtio_blk_req_hdr);
        vblk_dev.desc[0].flags = VRING_DESC_F_NEXT;
        vblk_dev.desc[0].next = 1;

        /* Descriptor 1: Data Buffer (Device Writes into bounce buffer) */
        vblk_dev.desc[1].addr = vblk_dev.bounce_phys;
        vblk_dev.desc[1].len = byte_count;
        vblk_dev.desc[1].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
        vblk_dev.desc[1].next = 2;

        /* Descriptor 2: Status Byte (Device Writes) */
        vblk_dev.desc[2].addr = vblk_dev.req_phys + 2048U;
        vblk_dev.desc[2].len = 1;
        vblk_dev.desc[2].flags = VRING_DESC_F_WRITE;
        vblk_dev.desc[2].next = 0;

        /* Add chain head (0) to available ring */
        uint16_t avail_idx = vblk_dev.avail->idx;
        vblk_dev.avail->ring[avail_idx % vblk_dev.qsize] = 0;
        __asm__ volatile ("" : : : "memory");
        vblk_dev.avail->idx = (uint16_t)(avail_idx + 1U);

        /* Notify device */
        outw(vblk_dev.iobase + VIRTIO_REG_QUEUE_NOTIFY, 0);

        /* Bounded timeout wait for completion */
        bool completed = false;
        for (uint32_t spin = 0; spin < 5000000U; spin++) {
            if (vblk_dev.used->idx != vblk_dev.last_used_idx) {
                completed = true;
                break;
            }
            __asm__ volatile ("pause");
        }

        if (!completed) {
            return -1;
        }

        /* Validate used ring completion element ownership and header ID */
        uint16_t used_slot = (uint16_t)(vblk_dev.last_used_idx % vblk_dev.qsize);
        uint32_t elem_id = vblk_dev.used->ring[used_slot].id;
        if (elem_id != 0U) {
            return -1;
        }

        vblk_dev.last_used_idx++;

        if (*vblk_dev.req_status != VIRTIO_BLK_S_OK) {
            return -1;
        }

        /* Copy received data to user buffer */
        mem_copy(dst, vblk_dev.bounce_virt, byte_count);

        dst += byte_count;
        cur_sector += chunk;
        remaining -= chunk;
    }

    return 0;
}

int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buf) {
    (void)sector;
    (void)count;
    (void)buf;
    /* Read-only block device: write requests strictly prohibited */
    return -1;
}

#ifdef DAWN_KERNEL_TEST
bool virtio_blk_self_test(void) {
    if (!vblk_dev.initialized) {
        if (!virtio_blk_initialize()) {
            return false;
        }
    }

    if (vblk_dev.capacity_sectors == 0U) {
        return false;
    }

    /* Transport validation: legacy I/O transport must match 0x1001, not modern 0x1042 */
    if (VIRTIO_BLK_PCI_DEVICE_ID != 0x1001U) {
        return false;
    }
    if (VIRTIO_BLK_PCI_MODERN_ID == VIRTIO_BLK_PCI_DEVICE_ID) {
        return false;
    }
    if (vblk_dev.qsize == 0U || vblk_dev.qsize > 1024U) {
        return false;
    }

    static uint8_t test_buf[2048] __attribute__((aligned(16)));

    /* Negative Test 1: Null buffer pointer */
    if (virtio_blk_read_sectors(0, 1, NULL) != -1) {
        return false;
    }

    /* Negative Test 2: Zero sector count */
    if (virtio_blk_read_sectors(0, 0, test_buf) != -1) {
        return false;
    }

    /* Negative Test 3: Read past capacity */
    if (virtio_blk_read_sectors(vblk_dev.capacity_sectors, 1, test_buf) != -1) {
        return false;
    }

    /* Negative Test 4: Sector index overflow */
    if (virtio_blk_read_sectors(UINT64_MAX, 1, test_buf) != -1) {
        return false;
    }

    /* Negative Test 5: Write rejection on read-only device */
    if (virtio_blk_write_sectors(0, 1, test_buf) != -1) {
        return false;
    }

    /* Negative Test 6: Block dev ops write rejection */
    if (vblk_block_dev.ops.write_blocks != NULL) {
        if (vblk_block_dev.ops.write_blocks(&vblk_block_dev, 0, 1, test_buf) != -1) {
            return false;
        }
    }

    /* Positive Test 1: Read sector 0 (first sector of disk) */
    mem_zero(test_buf, sizeof(test_buf));
    if (virtio_blk_read_sectors(0, 1, test_buf) != 0) {
        return false;
    }

    /* Positive Test 2: Read ISO9660 PVD at 2048-byte sector 16 (which is 512-byte sector 64) */
    mem_zero(test_buf, sizeof(test_buf));
    if (virtio_blk_read_sectors(64, 4, test_buf) != 0) {
        return false;
    }

    /* Check ISO9660 signature "CD001" at offset 1 */
    if (test_buf[0] != 1U ||
        test_buf[1] != 'C' || test_buf[2] != 'D' ||
        test_buf[3] != '0' || test_buf[4] != '0' || test_buf[5] != '1') {
        return false;
    }

    return true;
}
#endif
