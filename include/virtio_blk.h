/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_VIRTIO_BLK_H
#define DAWN_VIRTIO_BLK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_dev.h"

/* VirtIO PCI Vendor / Device IDs */
#define VIRTIO_PCI_VENDOR_ID     0x1AF4U
#define VIRTIO_BLK_PCI_DEVICE_ID 0x1001U
#define VIRTIO_BLK_PCI_MODERN_ID 0x1042U

/* VirtIO PCI Register Offsets (I/O space) */
#define VIRTIO_REG_DEVICE_FEATURES 0x00U /* 32-bit R */
#define VIRTIO_REG_GUEST_FEATURES  0x04U /* 32-bit R/W */
#define VIRTIO_REG_QUEUE_PFN       0x08U /* 32-bit R/W */
#define VIRTIO_REG_QUEUE_SIZE      0x0CU /* 16-bit R */
#define VIRTIO_REG_QUEUE_SEL       0x0EU /* 16-bit R/W */
#define VIRTIO_REG_QUEUE_NOTIFY    0x10U /* 16-bit R/W */
#define VIRTIO_REG_DEVICE_STATUS   0x12U /* 8-bit R/W */
#define VIRTIO_REG_ISR_STATUS      0x13U /* 8-bit R */
#define VIRTIO_REG_CONFIG_START    0x14U

/* Device Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE  1U
#define VIRTIO_STATUS_DRIVER       2U
#define VIRTIO_STATUS_DRIVER_OK    4U
#define VIRTIO_STATUS_FAILED       128U

/* VirtIO Block Features */
#define VIRTIO_BLK_F_SIZE_MAX  (1U << 1)
#define VIRTIO_BLK_F_SEG_MAX   (1U << 2)
#define VIRTIO_BLK_F_GEOMETRY  (1U << 4)
#define VIRTIO_BLK_F_RO        (1U << 5)
#define VIRTIO_BLK_F_BLK_SIZE  (1U << 6)
#define VIRTIO_BLK_F_FLUSH     (1U << 9)

/* Virtqueue ring descriptor flags */
#define VRING_DESC_F_NEXT  1U
#define VRING_DESC_F_WRITE 2U

/* Request types */
#define VIRTIO_BLK_T_IN           0U
#define VIRTIO_BLK_T_OUT          1U
#define VIRTIO_BLK_T_FLUSH        4U
#define VIRTIO_BLK_T_GET_ID       8U

/* Status types */
#define VIRTIO_BLK_S_OK     0U
#define VIRTIO_BLK_S_IOERR  1U
#define VIRTIO_BLK_S_UNSUPP 2U

#define VIRTIO_BLK_SECTOR_SIZE 512U
#define VIRTIO_BLK_MAX_SECTORS 128U /* 64 KiB bounce buffer */

struct virtio_blk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_blk_req_hdr) == 16, "virtio_blk_req_hdr must be 16 bytes");

struct virtio_blk_vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtio_blk_vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtio_blk_vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtio_blk_vring_used {
    uint16_t flags;
    uint16_t idx;
    struct virtio_blk_vring_used_elem ring[];
} __attribute__((packed));

struct virtio_blk_device {
    uint16_t iobase;
    uint16_t qsize;
    uint64_t capacity_sectors;
    bool read_only;
    bool initialized;

    uint64_t vq_phys;
    struct virtio_blk_vring_desc *desc;
    struct virtio_blk_vring_avail *avail;
    struct virtio_blk_vring_used *used;
    uint16_t last_used_idx;

    uint64_t req_phys;
    struct virtio_blk_req_hdr *req_hdr;
    volatile uint8_t *req_status;

    uint64_t bounce_phys;
    void *bounce_virt;
};

bool virtio_blk_initialize(void);
struct virtio_blk_device *virtio_blk_get_device(void);
int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buf);
int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buf);

#ifdef DAWN_KERNEL_TEST
bool virtio_blk_self_test(void);
#endif

#endif /* DAWN_VIRTIO_BLK_H */
