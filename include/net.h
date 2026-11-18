/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_NET_H
#define DAWN_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ETH_ALEN 6U
#define ETH_HLEN 14U

#define ETHERTYPE_IPV4 0x0800U
#define ETHERTYPE_ARP  0x0806U

#define IPPROTO_ICMP 1U
#define IPPROTO_TCP  6U
#define IPPROTO_UDP  17U

#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_FLAG_URG 0x20U

#define MAX_NET_DEVICES 4U
#define MAX_NET_SOCKETS 32U

/* Byte order helpers */
static inline uint16_t htons(uint16_t x) {
    return (uint16_t)(((x & 0xFFU) << 8) | ((x >> 8) & 0xFFU));
}
static inline uint16_t ntohs(uint16_t x) { return htons(x); }

static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFFU) << 24) |
           ((x & 0xFF00U) << 8) |
           ((x & 0xFF0000U) >> 8) |
           ((x >> 24) & 0xFFU);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

struct eth_header {
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed));

_Static_assert(sizeof(struct eth_header) == 14, "Ethernet header must be 14 bytes");

struct arp_header {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[ETH_ALEN];
    uint32_t spa;
    uint8_t  tha[ETH_ALEN];
    uint32_t tpa;
} __attribute__((packed));

_Static_assert(sizeof(struct arp_header) == 28, "ARP header must be 28 bytes");

struct ipv4_header {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed));

_Static_assert(sizeof(struct ipv4_header) == 20, "IPv4 header must be 20 bytes");

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

_Static_assert(sizeof(struct udp_header) == 8, "UDP header must be 8 bytes");

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

_Static_assert(sizeof(struct tcp_header) == 20, "TCP header must be 20 bytes");

enum socket_type {
    SOCK_STREAM = 1,
    SOCK_DGRAM  = 2,
    SOCK_RAW    = 3
};

enum tcp_state {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN = 1,
    TCP_STATE_SYN_SENT = 2,
    TCP_STATE_SYN_RECEIVED = 3,
    TCP_STATE_ESTABLISHED = 4,
    TCP_STATE_FIN_WAIT_1 = 5,
    TCP_STATE_FIN_WAIT_2 = 6,
    TCP_STATE_CLOSE_WAIT = 7,
    TCP_STATE_CLOSING = 8,
    TCP_STATE_LAST_ACK = 9,
    TCP_STATE_TIME_WAIT = 10
};

struct net_socket {
    uint32_t id;
    enum socket_type type;
    uint8_t protocol;
    enum tcp_state state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  recv_buffer[2048];
    size_t   recv_len;
    bool     bound;
    bool     connected;
    bool     active;
};

struct net_device {
    char name[16];
    uint8_t mac[ETH_ALEN];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    int (*send_packet)(struct net_device *dev, const void *buf, size_t len);
    bool up;
    bool active;
};

bool net_initialize(void);
struct net_device *net_get_default_device(void);
struct net_device *net_get_device(const char *name);

/* Socket API */
int net_socket_create(enum socket_type type, uint8_t protocol);
int net_socket_bind(int sock_id, uint32_t ip, uint16_t port);
int net_socket_connect(int sock_id, uint32_t ip, uint16_t port);
int net_socket_send(int sock_id, const void *data, size_t len);
int net_socket_recv(int sock_id, void *buf, size_t max_len);
int net_socket_close(int sock_id);

/* Packet injection / dispatch */
int net_receive_packet(struct net_device *dev, const void *buf, size_t len);

bool net_self_test(void);

#endif /* DAWN_NET_H */
