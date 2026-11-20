/* SPDX-License-Identifier: Unlicense */
#include "virtio_net.h"
#include "pci.h"
#include "physical_memory.h"
#include "limine_requests.h"

static struct virtio_net_device vnet_dev;

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

bool virtio_net_is_available(void) {
    return vnet_dev.initialized;
}

struct virtio_net_device *virtio_net_get_device(void) {
    return &vnet_dev;
}

static void dbg_char(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, (uint8_t)c);
}
static void dbg_str(const char *s) {
    while (*s) {
        if (*s == '\n') dbg_char('\r');
        dbg_char(*s++);
    }
}
static void dbg_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    dbg_str("0x");
    for (int i = 7; i >= 0; i--) {
        dbg_char(hex[(val >> (i * 4)) & 0x0F]);
    }
}

bool virtio_net_initialize(void) {
    if (vnet_dev.initialized) return true;

    pci_initialize();
    size_t dev_count = pci_get_device_count();
    const struct pci_device *target_pci = NULL;

    dbg_str("[VNET] Scanning PCI devices, count: ");
    dbg_hex((uint32_t)dev_count);
    dbg_str("\n");

    for (size_t i = 0; i < dev_count; i++) {
        const struct pci_device *pdev = pci_get_device(i);
        if (pdev != NULL) {
            dbg_str("  PCI dev: vendor=");
            dbg_hex(pdev->vendor_id);
            dbg_str(" device=");
            dbg_hex(pdev->device_id);
            dbg_str(" class=");
            dbg_hex(pdev->class_code);
            dbg_str(" bar0=");
            dbg_hex(pdev->bar[0]);
            dbg_str("\n");
            if (pdev->vendor_id == VIRTIO_PCI_VENDOR_ID) {
                target_pci = pdev;
            }
        }
    }

    if (target_pci == NULL) {
        dbg_str("[VNET] No virtio target found!\n");
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

    vnet_dev.iobase = iobase;

    /* 1. Reset device */
    outb(vnet_dev.iobase + VIRTIO_REG_DEVICE_STATUS, 0);

    /* 2. Set ACKNOWLEDGE */
    outb(vnet_dev.iobase + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* 3. Set DRIVER */
    outb(vnet_dev.iobase + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* 4. Feature negotiation */
    uint32_t host_features = inl(vnet_dev.iobase + VIRTIO_REG_DEVICE_FEATURES);
    outl(vnet_dev.iobase + VIRTIO_REG_GUEST_FEATURES, host_features & VIRTIO_NET_F_MAC);

    /* 5. Read MAC address */
    for (int i = 0; i < 6; i++) {
        vnet_dev.mac[i] = inb((uint16_t)(vnet_dev.iobase + VIRTIO_REG_CONFIG_START + (uint16_t)i));
    }
    if (vnet_dev.mac[0] == 0 && vnet_dev.mac[1] == 0 && vnet_dev.mac[2] == 0 &&
        vnet_dev.mac[3] == 0 && vnet_dev.mac[4] == 0 && vnet_dev.mac[5] == 0) {
        vnet_dev.mac[0] = 0x52; vnet_dev.mac[1] = 0x54; vnet_dev.mac[2] = 0x00;
        vnet_dev.mac[3] = 0x12; vnet_dev.mac[4] = 0x34; vnet_dev.mac[5] = 0x56;
    }

    if (limine_hhdm_request.response == NULL) {
        return false;
    }
    uint64_t hhdm = limine_hhdm_request.response->offset;

    /* 6. Setup RX Queue (Queue 0) */
    outw(vnet_dev.iobase + VIRTIO_REG_QUEUE_SEL, 0);
    vnet_dev.rx_qsize = inw(vnet_dev.iobase + VIRTIO_REG_QUEUE_SIZE);
    if (vnet_dev.rx_qsize == 0) return false;

    size_t rx_desc_size = (size_t)vnet_dev.rx_qsize * sizeof(struct vring_desc);
    size_t rx_avail_size = sizeof(uint16_t) * (3U + (size_t)vnet_dev.rx_qsize);
    size_t rx_used_offset = (rx_desc_size + rx_avail_size + 4095U) & ~4095U;
    size_t rx_used_size = sizeof(uint16_t) * 3U + (size_t)vnet_dev.rx_qsize * sizeof(struct vring_used_elem);
    size_t rx_total = rx_used_offset + rx_used_size;
    size_t rx_pages = (rx_total + 4095U) / 4096U;

    if (!physical_allocator_allocate_contiguous(rx_pages, &vnet_dev.rx_vq_phys)) {
        return false;
    }
    uint8_t *rx_virt = (uint8_t *)(hhdm + vnet_dev.rx_vq_phys);
    mem_zero(rx_virt, rx_pages * 4096U);

    vnet_dev.rx_desc = (struct vring_desc *)rx_virt;
    vnet_dev.rx_avail = (struct vring_avail *)(rx_virt + rx_desc_size);
    vnet_dev.rx_used = (struct vring_used *)(rx_virt + rx_used_offset);
    outl(vnet_dev.iobase + VIRTIO_REG_QUEUE_PFN, (uint32_t)(vnet_dev.rx_vq_phys >> 12));

    /* 7. Setup TX Queue (Queue 1) */
    outw(vnet_dev.iobase + VIRTIO_REG_QUEUE_SEL, 1);
    vnet_dev.tx_qsize = inw(vnet_dev.iobase + VIRTIO_REG_QUEUE_SIZE);
    if (vnet_dev.tx_qsize == 0) return false;

    size_t tx_desc_size = (size_t)vnet_dev.tx_qsize * sizeof(struct vring_desc);
    size_t tx_avail_size = sizeof(uint16_t) * (3U + (size_t)vnet_dev.tx_qsize);
    size_t tx_used_offset = (tx_desc_size + tx_avail_size + 4095U) & ~4095U;
    size_t tx_used_size = sizeof(uint16_t) * 3U + (size_t)vnet_dev.tx_qsize * sizeof(struct vring_used_elem);
    size_t tx_total = tx_used_offset + tx_used_size;
    size_t tx_pages = (tx_total + 4095U) / 4096U;

    if (!physical_allocator_allocate_contiguous(tx_pages, &vnet_dev.tx_vq_phys)) {
        return false;
    }
    uint8_t *tx_virt = (uint8_t *)(hhdm + vnet_dev.tx_vq_phys);
    mem_zero(tx_virt, tx_pages * 4096U);

    vnet_dev.tx_desc = (struct vring_desc *)tx_virt;
    vnet_dev.tx_avail = (struct vring_avail *)(tx_virt + tx_desc_size);
    vnet_dev.tx_used = (struct vring_used *)(tx_virt + tx_used_offset);
    outl(vnet_dev.iobase + VIRTIO_REG_QUEUE_PFN, (uint32_t)(vnet_dev.tx_vq_phys >> 12));

    /* 8. Allocate RX buffers (32 buffers * 2048 bytes = 16 pages) */
    if (!physical_allocator_allocate_contiguous(16, &vnet_dev.rx_buffers_phys)) {
        return false;
    }
    vnet_dev.rx_buffers_virt = (void *)(hhdm + vnet_dev.rx_buffers_phys);
    mem_zero(vnet_dev.rx_buffers_virt, 16U * 4096U);

    for (uint16_t i = 0; i < 32 && i < vnet_dev.rx_qsize; i++) {
        vnet_dev.rx_desc[i].addr = vnet_dev.rx_buffers_phys + (uint64_t)i * 2048ULL;
        vnet_dev.rx_desc[i].len = 2048U;
        vnet_dev.rx_desc[i].flags = VRING_DESC_F_WRITE;
        vnet_dev.rx_desc[i].next = 0;
        vnet_dev.rx_avail->ring[i] = i;
    }
    vnet_dev.rx_avail->flags = 0;
    vnet_dev.rx_avail->idx = 32;

    /* 9. Allocate TX buffer (1 page) */
    if (!physical_allocator_allocate_contiguous(1, &vnet_dev.tx_buffer_phys)) {
        return false;
    }
    vnet_dev.tx_buffer_virt = (void *)(hhdm + vnet_dev.tx_buffer_phys);
    mem_zero(vnet_dev.tx_buffer_virt, 4096U);

    vnet_dev.last_rx_used = 0;
    vnet_dev.last_tx_used = 0;

    /* 10. Notify RX Queue */
    outw(vnet_dev.iobase + VIRTIO_REG_QUEUE_NOTIFY, 0);

    /* 11. Set DRIVER_OK */
    outb(vnet_dev.iobase + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    vnet_dev.initialized = true;
    return true;
}

int virtio_net_send_packet(struct net_device *dev, const void *buf, size_t len) {
    (void)dev;
    if (!vnet_dev.initialized || buf == NULL || len == 0 || len > 1514) return -1;

    uint8_t *tx_virt = (uint8_t *)vnet_dev.tx_buffer_virt;
    /* Clear 10-byte virtio_net_hdr */
    for (size_t i = 0; i < sizeof(struct virtio_net_hdr); i++) {
        tx_virt[i] = 0;
    }
    /* Copy Ethernet frame */
    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        tx_virt[sizeof(struct virtio_net_hdr) + i] = src[i];
    }

    /* Single descriptor in TX queue */
    vnet_dev.tx_desc[0].addr = vnet_dev.tx_buffer_phys;
    vnet_dev.tx_desc[0].len = (uint32_t)(sizeof(struct virtio_net_hdr) + len);
    vnet_dev.tx_desc[0].flags = 0;
    vnet_dev.tx_desc[0].next = 0;

    uint16_t avail_idx = (uint16_t)(vnet_dev.tx_avail->idx % vnet_dev.tx_qsize);
    vnet_dev.tx_avail->ring[avail_idx] = 0;
    __asm__ volatile ("" ::: "memory");
    vnet_dev.tx_avail->idx++;
    __asm__ volatile ("" ::: "memory");

    /* Kick TX queue */
    outw(vnet_dev.iobase + VIRTIO_REG_QUEUE_NOTIFY, 1);

    /* Spin until packet is sent */
    uint16_t target_used = (uint16_t)(vnet_dev.last_tx_used + 1);
    uint32_t timeout = 2000000;
    while (vnet_dev.tx_used->idx != target_used && timeout > 0) {
        __asm__ volatile ("pause" ::: "memory");
        timeout--;
    }
    vnet_dev.last_tx_used = vnet_dev.tx_used->idx;
    return (int)len;
}

int virtio_net_poll(struct net_device *dev) {
    if (!vnet_dev.initialized || dev == NULL) return 0;

    int packets_processed = 0;
    while (vnet_dev.last_rx_used != vnet_dev.rx_used->idx) {
        uint16_t used_idx = (uint16_t)(vnet_dev.last_rx_used % vnet_dev.rx_qsize);
        uint32_t desc_id = vnet_dev.rx_used->ring[used_idx].id;
        uint32_t len = vnet_dev.rx_used->ring[used_idx].len;

        if (desc_id < 32 && len > sizeof(struct virtio_net_hdr)) {
            uint8_t *buf = (uint8_t *)vnet_dev.rx_buffers_virt + (size_t)desc_id * 2048U;
            uint8_t *packet = buf + sizeof(struct virtio_net_hdr);
            size_t packet_len = (size_t)(len - sizeof(struct virtio_net_hdr));

            net_receive_packet(dev, packet, packet_len);
            packets_processed++;

            /* Return descriptor to available ring */
            uint16_t avail_idx = (uint16_t)(vnet_dev.rx_avail->idx % vnet_dev.rx_qsize);
            vnet_dev.rx_avail->ring[avail_idx] = (uint16_t)desc_id;
            __asm__ volatile ("" ::: "memory");
            vnet_dev.rx_avail->idx++;
            __asm__ volatile ("" ::: "memory");
        }
        vnet_dev.last_rx_used++;
    }

    if (packets_processed > 0) {
        outw(vnet_dev.iobase + VIRTIO_REG_QUEUE_NOTIFY, 0);
    }
    return packets_processed;
}

#ifdef DAWN_KERNEL_TEST
bool virtio_net_self_test(void) {
    dbg_str("[VNET] Starting self test\n");
    if (!virtio_net_initialize()) {
        dbg_str("[VNET] Init failed\n");
        return false;
    }
    struct net_device *eth0 = net_get_device("eth0");
    if (eth0 == NULL || !eth0->up) {
        dbg_str("[VNET] eth0 is NULL or not up\n");
        return false;
    }

    /* Send ARP request for QEMU Gateway (10.0.2.2) */
    uint32_t gw_ip = eth0->gateway;
    if (gw_ip == 0) gw_ip = htonl(0x0A000202U);

    dbg_str("[VNET] Sending ARP request to gateway\n");
    if (net_send_arp_request(eth0, gw_ip) != 0) {
        dbg_str("[VNET] ARP send failed\n");
        return false;
    }

    /* Poll for ARP reply */
    uint32_t retries = 200000;
    while (retries > 0 && !net_arp_cache_lookup(gw_ip, NULL)) {
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
        retries--;
    }
    if (!net_arp_cache_lookup(gw_ip, NULL)) {
        dbg_str("[VNET] ARP lookup timed out\n");
        return false;
    }
    dbg_str("[VNET] ARP reply received!\n");

    /* Send ICMP Echo Request to Gateway */
    uint32_t prev_replies = net_get_icmp_reply_count();
    if (net_send_icmp_echo(eth0, gw_ip, 42, "PING", 4) != 0) {
        dbg_str("[VNET] ICMP echo send failed\n");
        return false;
    }

    /* Poll for ICMP Echo Reply */
    retries = 200000;
    while (retries > 0 && net_get_icmp_reply_count() == prev_replies) {
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
        retries--;
    }
    if (net_get_icmp_reply_count() == prev_replies) {
        dbg_str("[VNET] ICMP reply timed out\n");
        return false;
    }
    dbg_str("[VNET] ICMP reply received!\n");

    return true;
}
#endif
