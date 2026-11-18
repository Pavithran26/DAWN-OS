/* SPDX-License-Identifier: Unlicense */
#include "net.h"
#include "device.h"

static struct net_device net_devices[MAX_NET_DEVICES];
static struct net_socket net_sockets[MAX_NET_SOCKETS];
static uint32_t next_sock_id = 1;
static bool net_initialized = false;

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

static uint16_t net_checksum(const void *data, size_t len) {
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *words++;
        len -= 2;
    }
    if (len > 0) {
        sum += *(const uint8_t *)words;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* Loopback interface */
static int loopback_send(struct net_device *dev, const void *buf, size_t len) {
    if (dev == NULL || buf == NULL) return -1;
    return net_receive_packet(dev, buf, len);
}

bool net_initialize(void) {
    if (net_initialized) return true;

    for (size_t i = 0; i < MAX_NET_DEVICES; i++) net_devices[i] = (struct net_device){0};
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) net_sockets[i] = (struct net_socket){0};
    next_sock_id = 1;

    /* Initialize Loopback (lo) */
    struct net_device *lo = &net_devices[0];
    str_copy(lo->name, "lo", sizeof(lo->name));
    lo->mac[0] = 0x00; lo->mac[1] = 0x00; lo->mac[2] = 0x00;
    lo->mac[3] = 0x00; lo->mac[4] = 0x00; lo->mac[5] = 0x00;
    lo->ip = htonl(0x7F000001U); /* 127.0.0.1 */
    lo->netmask = htonl(0xFF000000U);
    lo->gateway = 0;
    lo->send_packet = loopback_send;
    lo->up = true;
    lo->active = true;

    /* Initialize default Ethernet device eth0 */
    struct net_device *eth0 = &net_devices[1];
    str_copy(eth0->name, "eth0", sizeof(eth0->name));
    eth0->mac[0] = 0x52; eth0->mac[1] = 0x54; eth0->mac[2] = 0x00;
    eth0->mac[3] = 0x12; eth0->mac[4] = 0x34; eth0->mac[5] = 0x56;
    eth0->ip = htonl(0x0A00020FU); /* 10.0.2.15 (QEMU default) */
    eth0->netmask = htonl(0xFFFFFF00U);
    eth0->gateway = htonl(0x0A000202U);
    eth0->send_packet = loopback_send; /* Fallback to software reflection */
    eth0->up = true;
    eth0->active = true;

    /* Register network device in Device Manager */
    struct device dev = {0};
    str_copy(dev.name, "net0", DEVICE_NAME_MAX);
    dev.bus = BUS_TYPE_PLATFORM;
    dev.class_type = DEV_CLASS_NETWORK;
    dev.status = DEV_STATUS_RUNNING;
    device_register(&dev);

    net_initialized = true;
    return true;
}

struct net_device *net_get_default_device(void) {
    if (!net_initialized) net_initialize();
    return &net_devices[1]; /* eth0 */
}

struct net_device *net_get_device(const char *name) {
    if (!net_initialized) net_initialize();
    if (name == NULL) return NULL;
    for (size_t i = 0; i < MAX_NET_DEVICES; i++) {
        if (net_devices[i].active && str_equal(net_devices[i].name, name)) {
            return &net_devices[i];
        }
    }
    return NULL;
}

int net_receive_packet(struct net_device *dev, const void *buf, size_t len) {
    if (dev == NULL || buf == NULL || len < sizeof(struct eth_header)) return -1;
    const uint8_t *packet = (const uint8_t *)buf;
    const struct eth_header *eth = (const struct eth_header *)packet;

    uint16_t eth_type = ntohs(eth->type);

    /* ARP Handler */
    if (eth_type == ETHERTYPE_ARP && len >= sizeof(struct eth_header) + sizeof(struct arp_header)) {
        const struct arp_header *arp = (const struct arp_header *)(packet + sizeof(struct eth_header));
        if (ntohs(arp->oper) == 1 && arp->tpa == dev->ip) {
            /* Send ARP reply */
            uint8_t reply_buf[sizeof(struct eth_header) + sizeof(struct arp_header)];
            struct eth_header *reth = (struct eth_header *)reply_buf;
            struct arp_header *rarp = (struct arp_header *)(reply_buf + sizeof(struct eth_header));

            for (int i = 0; i < 6; i++) {
                reth->dest[i] = eth->src[i];
                reth->src[i] = dev->mac[i];
            }
            reth->type = htons(ETHERTYPE_ARP);

            rarp->htype = htons(1);
            rarp->ptype = htons(ETHERTYPE_IPV4);
            rarp->hlen = 6;
            rarp->plen = 4;
            rarp->oper = htons(2); /* ARP Reply */
            for (int i = 0; i < 6; i++) {
                rarp->sha[i] = dev->mac[i];
                rarp->tha[i] = arp->sha[i];
            }
            rarp->spa = dev->ip;
            rarp->tpa = arp->spa;

            if (dev->send_packet) {
                dev->send_packet(dev, reply_buf, sizeof(reply_buf));
            }
            return 0;
        }
    }

    /* IPv4 Handler */
    if (eth_type == ETHERTYPE_IPV4 && len >= sizeof(struct eth_header) + sizeof(struct ipv4_header)) {
        const struct ipv4_header *ip = (const struct ipv4_header *)(packet + sizeof(struct eth_header));
        size_t ip_hdr_len = (size_t)((ip->ver_ihl & 0x0FU) * 4U);
        if (len < sizeof(struct eth_header) + ip_hdr_len) return -1;

        /* ICMP Protocol */
        if (ip->protocol == IPPROTO_ICMP && len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct icmp_header)) {
            const struct icmp_header *icmp = (const struct icmp_header *)(packet + sizeof(struct eth_header) + ip_hdr_len);
            if (icmp->type == 8) {
                /* Echo Request -> Send Echo Reply */
                size_t icmp_data_len = len - (sizeof(struct eth_header) + ip_hdr_len);
                uint8_t reply_buf[1500];
                if (sizeof(struct eth_header) + sizeof(struct ipv4_header) + icmp_data_len > sizeof(reply_buf)) return -1;

                struct eth_header *reth = (struct eth_header *)reply_buf;
                struct ipv4_header *rip = (struct ipv4_header *)(reply_buf + sizeof(struct eth_header));
                struct icmp_header *ricmp = (struct icmp_header *)(reply_buf + sizeof(struct eth_header) + sizeof(struct ipv4_header));

                for (int i = 0; i < 6; i++) {
                    reth->dest[i] = eth->src[i];
                    reth->src[i] = dev->mac[i];
                }
                reth->type = htons(ETHERTYPE_IPV4);

                *rip = *ip;
                rip->src_ip = ip->dest_ip;
                rip->dest_ip = ip->src_ip;
                rip->checksum = 0;
                rip->checksum = net_checksum(rip, sizeof(struct ipv4_header));

                /* Copy ICMP payload */
                const uint8_t *src_icmp_data = (const uint8_t *)icmp;
                uint8_t *dst_icmp_data = (uint8_t *)ricmp;
                for (size_t i = 0; i < icmp_data_len; i++) dst_icmp_data[i] = src_icmp_data[i];

                ricmp->type = 0; /* Echo Reply */
                ricmp->code = 0;
                ricmp->checksum = 0;
                ricmp->checksum = net_checksum(ricmp, icmp_data_len);

                size_t total_reply_len = sizeof(struct eth_header) + sizeof(struct ipv4_header) + icmp_data_len;
                if (dev->send_packet) {
                    dev->send_packet(dev, reply_buf, total_reply_len);
                }
                return 0;
            }
        }

        /* UDP Protocol */
        if (ip->protocol == IPPROTO_UDP && len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct udp_header)) {
            const struct udp_header *udp = (const struct udp_header *)(packet + sizeof(struct eth_header) + ip_hdr_len);
            uint16_t dport = ntohs(udp->dst_port);
            size_t payload_len = (size_t)ntohs(udp->length) - sizeof(struct udp_header);
            const uint8_t *payload = packet + sizeof(struct eth_header) + ip_hdr_len + sizeof(struct udp_header);

            for (size_t s = 0; s < MAX_NET_SOCKETS; s++) {
                if (net_sockets[s].active && net_sockets[s].type == SOCK_DGRAM && net_sockets[s].local_port == dport) {
                    size_t to_copy = (payload_len < sizeof(net_sockets[s].recv_buffer)) ? payload_len : sizeof(net_sockets[s].recv_buffer);
                    for (size_t b = 0; b < to_copy; b++) net_sockets[s].recv_buffer[b] = payload[b];
                    net_sockets[s].recv_len = to_copy;
                    net_sockets[s].remote_ip = ip->src_ip;
                    net_sockets[s].remote_port = ntohs(udp->src_port);
                    return 0;
                }
            }
        }

        /* TCP Protocol */
        if (ip->protocol == IPPROTO_TCP && len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct tcp_header)) {
            const struct tcp_header *tcp = (const struct tcp_header *)(packet + sizeof(struct eth_header) + ip_hdr_len);
            uint16_t dport = ntohs(tcp->dst_port);

            for (size_t s = 0; s < MAX_NET_SOCKETS; s++) {
                if (net_sockets[s].active && net_sockets[s].type == SOCK_STREAM && net_sockets[s].local_port == dport) {
                    if ((tcp->flags & TCP_FLAG_SYN) != 0) {
                        net_sockets[s].state = TCP_STATE_SYN_RECEIVED;
                        net_sockets[s].remote_ip = ip->src_ip;
                        net_sockets[s].remote_port = ntohs(tcp->src_port);
                        net_sockets[s].ack = ntohl(tcp->seq_num) + 1;
                        net_sockets[s].seq = 1000;
                    } else if ((tcp->flags & TCP_FLAG_ACK) != 0 && net_sockets[s].state == TCP_STATE_SYN_RECEIVED) {
                        net_sockets[s].state = TCP_STATE_ESTABLISHED;
                    } else if ((tcp->flags & TCP_FLAG_FIN) != 0) {
                        net_sockets[s].state = TCP_STATE_CLOSE_WAIT;
                    }
                    return 0;
                }
            }
        }
    }
    return 0;
}

int net_socket_create(enum socket_type type, uint8_t protocol) {
    if (!net_initialized) net_initialize();
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (!net_sockets[i].active) {
            struct net_socket *sock = &net_sockets[i];
            *sock = (struct net_socket){0};
            sock->id = next_sock_id++;
            sock->type = type;
            sock->protocol = protocol;
            sock->state = TCP_STATE_CLOSED;
            sock->active = true;
            return (int)sock->id;
        }
    }
    return -1;
}

int net_socket_bind(int sock_id, uint32_t ip, uint16_t port) {
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (net_sockets[i].active && net_sockets[i].id == (uint32_t)sock_id) {
            net_sockets[i].local_ip = ip;
            net_sockets[i].local_port = port;
            net_sockets[i].bound = true;
            if (net_sockets[i].type == SOCK_STREAM) {
                net_sockets[i].state = TCP_STATE_LISTEN;
            }
            return 0;
        }
    }
    return -1;
}

int net_socket_connect(int sock_id, uint32_t ip, uint16_t port) {
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (net_sockets[i].active && net_sockets[i].id == (uint32_t)sock_id) {
            net_sockets[i].remote_ip = ip;
            net_sockets[i].remote_port = port;
            net_sockets[i].connected = true;
            if (net_sockets[i].type == SOCK_STREAM) {
                net_sockets[i].state = TCP_STATE_ESTABLISHED;
            }
            return 0;
        }
    }
    return -1;
}

int net_socket_send(int sock_id, const void *data, size_t len) {
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (net_sockets[i].active && net_sockets[i].id == (uint32_t)sock_id) {
            (void)data;
            return (int)len;
        }
    }
    return -1;
}

int net_socket_recv(int sock_id, void *buf, size_t max_len) {
    if (buf == NULL) return -1;
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (net_sockets[i].active && net_sockets[i].id == (uint32_t)sock_id) {
            if (net_sockets[i].recv_len == 0) return 0;
            size_t to_read = (net_sockets[i].recv_len < max_len) ? net_sockets[i].recv_len : max_len;
            uint8_t *dst = (uint8_t *)buf;
            for (size_t b = 0; b < to_read; b++) dst[b] = net_sockets[i].recv_buffer[b];
            net_sockets[i].recv_len = 0;
            return (int)to_read;
        }
    }
    return -1;
}

int net_socket_close(int sock_id) {
    for (size_t i = 0; i < MAX_NET_SOCKETS; i++) {
        if (net_sockets[i].active && net_sockets[i].id == (uint32_t)sock_id) {
            net_sockets[i].active = false;
            net_sockets[i].state = TCP_STATE_CLOSED;
            return 0;
        }
    }
    return -1;
}

/* Self-test */
bool net_self_test(void) {
    if (!net_initialize()) return false;

    /* Checksum verification (standard RFC 1071 test) */
    uint16_t test_data[4] = {0x0001, 0xF203, 0xF4F5, 0xF6F7};
    uint16_t cs = net_checksum(test_data, 8);
    if (cs == 0) return false;

    /* Byte order helpers */
    if (htons(0x1234) != 0x3412) return false;
    if (htonl(0x12345678) != 0x78563412) return false;

    /* Loopback device test */
    struct net_device *lo = net_get_device("lo");
    if (lo == NULL || !lo->up || lo->ip != htonl(0x7F000001U)) return false;

    /* Socket lifecycle test */
    int sock = net_socket_create(SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;
    if (net_socket_bind(sock, lo->ip, 8080) != 0) return false;

    /* Build synthetic UDP packet sent to port 8080 */
    uint8_t pkt[sizeof(struct eth_header) + sizeof(struct ipv4_header) + sizeof(struct udp_header) + 4] = {0};
    struct eth_header *eh = (struct eth_header *)pkt;
    struct ipv4_header *ih = (struct ipv4_header *)(pkt + sizeof(struct eth_header));
    struct udp_header *uh = (struct udp_header *)(pkt + sizeof(struct eth_header) + sizeof(struct ipv4_header));
    uint8_t *payload = pkt + sizeof(struct eth_header) + sizeof(struct ipv4_header) + sizeof(struct udp_header);

    eh->type = htons(ETHERTYPE_IPV4);
    ih->ver_ihl = 0x45;
    ih->protocol = IPPROTO_UDP;
    ih->total_length = htons((uint16_t)(sizeof(struct ipv4_header) + sizeof(struct udp_header) + 4));
    ih->dest_ip = lo->ip;
    uh->dst_port = htons(8080);
    uh->src_port = htons(12345);
    uh->length = htons(sizeof(struct udp_header) + 4);
    payload[0] = 'D'; payload[1] = 'A'; payload[2] = 'W'; payload[3] = 'N';

    /* Deliver packet */
    if (net_receive_packet(lo, pkt, sizeof(pkt)) != 0) return false;

    /* Read from socket */
    char recv_buf[8] = {0};
    int r = net_socket_recv(sock, recv_buf, sizeof(recv_buf));
    if (r != 4) return false;
    if (recv_buf[0] != 'D' || recv_buf[1] != 'A' || recv_buf[2] != 'W' || recv_buf[3] != 'N') return false;

    net_socket_close(sock);
    return true;
}
