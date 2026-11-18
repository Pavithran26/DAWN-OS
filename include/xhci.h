/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_XHCI_H
#define DAWN_XHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pci.h"

#define XHCI_MAX_PORTS 16U
#define XHCI_MAX_SLOTS 32U

/* TRB Types */
#define XHCI_TRB_NORMAL            1U
#define XHCI_TRB_SETUP_STAGE       2U
#define XHCI_TRB_DATA_STAGE        3U
#define XHCI_TRB_STATUS_STAGE      4U
#define XHCI_TRB_LINK              6U
#define XHCI_TRB_ENABLE_SLOT       9U
#define XHCI_TRB_DISABLE_SLOT      10U
#define XHCI_TRB_ADDRESS_DEVICE    11U
#define XHCI_TRB_CONFIG_ENDPOINT   12U
#define XHCI_TRB_TRANSFER_EVENT    32U
#define XHCI_TRB_COMMAND_COMPLETION 33U
#define XHCI_TRB_PORT_STATUS_CHANGE 34U

/* Port Speed */
#define XHCI_SPEED_FULL_SPEED  1U
#define XHCI_SPEED_LOW_SPEED   2U
#define XHCI_SPEED_HIGH_SPEED  3U
#define XHCI_SPEED_SUPER_SPEED 4U

struct xhci_cap_regs {
    volatile uint8_t  caplength;
    volatile uint8_t  reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hcsparams1;
    volatile uint32_t hcsparams2;
    volatile uint32_t hcsparams3;
    volatile uint32_t hccparams1;
    volatile uint32_t dboff;
    volatile uint32_t rtsoff;
    volatile uint32_t hccparams2;
} __attribute__((packed));

struct xhci_op_regs {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t pagesize;
    volatile uint32_t reserved0[2];
    volatile uint32_t dnctrl;
    volatile uint64_t crcr;
    volatile uint32_t reserved1[4];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
} __attribute__((packed));

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

_Static_assert(sizeof(struct xhci_trb) == 16, "xHCI TRB must be exactly 16 bytes");

struct xhci_port {
    uint32_t port_number;
    uint8_t  speed;
    bool     connected;
    bool     enabled;
    bool     power;
};

struct xhci_controller {
    struct xhci_cap_regs *cap;
    struct xhci_op_regs  *op;
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t max_intrs;
    struct xhci_port ports[XHCI_MAX_PORTS];
    bool active;
};

bool xhci_initialize(void);
size_t xhci_get_controller_count(void);
struct xhci_controller *xhci_get_controller(size_t index);
int xhci_poll_events(void);

bool xhci_self_test(void);

#endif /* DAWN_XHCI_H */
