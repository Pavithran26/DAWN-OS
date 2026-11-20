/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_VIRTIO_NET_H
#define DAWN_VIRTIO_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "net.h"

/* VirtIO PCI Vendor / Device IDs */
#define VIRTIO_PCI_VENDOR_ID     0x1AF4U
#define VIRTIO_NET_PCI_DEVICE_ID 0x1000U

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

/* VirtIO Features */
#define VIRTIO_NET_F_MAC (1U << 5)

/* Virtqueue ring descriptor flags */
#define VRING_DESC_F_NEXT  1U
#define VRING_DESC_F_WRITE 2U

struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_net_hdr) == 10, "virtio_net_hdr must be 10 bytes in legacy VirtIO");

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

struct virtio_net_device {
    uint16_t iobase;
    uint8_t  mac[ETH_ALEN];
    bool     initialized;
    uint16_t rx_qsize;
    uint16_t tx_qsize;
    uint16_t last_rx_used;
    uint16_t last_tx_used;

    /* Pointers to virtqueue memory (mapped via HHDM) */
    struct vring_desc  *rx_desc;
    struct vring_avail *rx_avail;
    struct vring_used  *rx_used;

    struct vring_desc  *tx_desc;
    struct vring_avail *tx_avail;
    struct vring_used  *tx_used;

    /* Physical addresses of virtqueue pages */
    uint64_t rx_vq_phys;
    uint64_t tx_vq_phys;

    /* Buffer storage */
    uint64_t rx_buffers_phys;
    void    *rx_buffers_virt;
    uint64_t tx_buffer_phys;
    void    *tx_buffer_virt;
};

bool virtio_net_initialize(void);
bool virtio_net_is_available(void);
struct virtio_net_device *virtio_net_get_device(void);
int virtio_net_send_packet(struct net_device *dev, const void *buf, size_t len);
int virtio_net_poll(struct net_device *dev);

#ifdef DAWN_KERNEL_TEST
bool virtio_net_self_test(void);
#endif

#endif /* DAWN_VIRTIO_NET_H */
