/* SPDX-License-Identifier: Unlicense */
#include "xhci.h"
#include "device.h"

#define XHCI_MAX_CONTROLLERS 2U

static struct xhci_controller controllers[XHCI_MAX_CONTROLLERS];
static size_t controller_count = 0;
static bool xhci_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

bool xhci_initialize(void) {
    if (xhci_initialized) return true;
    for (size_t i = 0; i < XHCI_MAX_CONTROLLERS; i++) {
        controllers[i] = (struct xhci_controller){0};
    }
    controller_count = 0;

    struct pci_device xhci_pci[XHCI_MAX_CONTROLLERS];
    size_t count = pci_find_by_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_SERIAL_USB, PCI_PROGIF_USB_XHCI, xhci_pci, XHCI_MAX_CONTROLLERS);

    for (size_t i = 0; i < count && i < XHCI_MAX_CONTROLLERS; i++) {
        uint32_t bar0 = xhci_pci[i].bar[0];
        uint32_t bar1 = xhci_pci[i].bar[1];
        if (bar0 == 0 || (bar0 & 1U) != 0) continue;

        uint64_t mmio_base = (uint64_t)(bar0 & ~0x0FU);
        if ((bar0 & 0x04U) != 0) {
            mmio_base |= ((uint64_t)bar1) << 32;
        }

        struct xhci_controller *ctrl = &controllers[controller_count];
        ctrl->cap = (struct xhci_cap_regs *)mmio_base;
        ctrl->op = (struct xhci_op_regs *)(mmio_base + ctrl->cap->caplength);
        ctrl->active = true;

        uint32_t p1 = ctrl->cap->hcsparams1;
        ctrl->max_slots = p1 & 0xFFU;
        ctrl->max_intrs = (p1 >> 8) & 0x7FFU;
        ctrl->max_ports = (p1 >> 24) & 0xFFU;
        if (ctrl->max_ports > XHCI_MAX_PORTS) ctrl->max_ports = XHCI_MAX_PORTS;

        /* Register in Device Manager */
        struct device xhci_dev = {0};
        str_copy(xhci_dev.name, "xhci0", DEVICE_NAME_MAX);
        xhci_dev.bus = BUS_TYPE_PCI;
        xhci_dev.class_type = DEV_CLASS_SERIAL;
        xhci_dev.resource.mmio_base = mmio_base;
        xhci_dev.resource.mmio_size = 0x10000;
        xhci_dev.resource.irq_line = xhci_pci[i].irq_line;
        device_register(&xhci_dev);

        controller_count++;
    }

    xhci_initialized = true;
    return true;
}

size_t xhci_get_controller_count(void) {
    return controller_count;
}

struct xhci_controller *xhci_get_controller(size_t index) {
    if (index >= controller_count) return NULL;
    return &controllers[index];
}

int xhci_poll_events(void) {
    /* Event ring polling for active transfers */
    return 0;
}

/* Self-test */
bool xhci_self_test(void) {
    /* Test TRB size and packing */
    if (sizeof(struct xhci_trb) != 16) return false;

    /* Build command TRB: Enable Slot Command */
    struct xhci_trb cmd_trb = {0};
    cmd_trb.parameter = 0;
    cmd_trb.status = 0;
    cmd_trb.control = (XHCI_TRB_ENABLE_SLOT << 10) | 1U; /* Slot type 0, TRB type, cycle bit */

    uint32_t trb_type = (cmd_trb.control >> 10) & 0x3FU;
    if (trb_type != XHCI_TRB_ENABLE_SLOT) return false;
    if ((cmd_trb.control & 1U) == 0) return false;

    /* Build Transfer Event TRB */
    struct xhci_trb event_trb = {0};
    event_trb.parameter = 0x1000ULL; /* TRB Pointer */
    event_trb.status = (0x01U << 24) | 64U; /* Success completion code, bytes transferred */
    event_trb.control = (XHCI_TRB_TRANSFER_EVENT << 10) | 1U;

    uint32_t event_type = (event_trb.control >> 10) & 0x3FU;
    uint8_t completion_code = (uint8_t)(event_trb.status >> 24);
    if (event_type != XHCI_TRB_TRANSFER_EVENT) return false;
    if (completion_code != 0x01U) return false;

    return true;
}
