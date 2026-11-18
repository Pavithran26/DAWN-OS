/* SPDX-License-Identifier: Unlicense */
#include "usb_hid.h"
#include "device.h"

static int32_t hid_mouse_x = 512;
static int32_t hid_mouse_y = 384;
static uint32_t hid_bound_x = 1024;
static uint32_t hid_bound_y = 768;

static const char hid_unshifted[256] = {
    0, 0, 0, 0,
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/',
    0
};

static const char hid_shifted[256] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?',
    0
};

bool usb_hid_initialize(void) {
    /* Register USB HID Driver in Device Manager */
    struct driver hid_drv = {0};
    hid_drv.name[0] = 'u'; hid_drv.name[1] = 's'; hid_drv.name[2] = 'b';
    hid_drv.name[3] = '_'; hid_drv.name[4] = 'h'; hid_drv.name[5] = 'i';
    hid_drv.name[6] = 'd'; hid_drv.name[7] = '\0';
    hid_drv.bus = BUS_TYPE_USB;
    hid_drv.class_type = DEV_CLASS_INPUT;
    driver_register(&hid_drv);
    return true;
}

void usb_hid_process_keyboard_report(const struct usb_hid_keyboard_report *report, struct key_event *out_event) {
    if (report == NULL || out_event == NULL) return;
    *out_event = (struct key_event){0};

    bool shift = (report->modifiers & (USB_HID_MOD_LSHIFT | USB_HID_MOD_RSHIFT)) != 0;
    bool ctrl  = (report->modifiers & (USB_HID_MOD_LCTRL | USB_HID_MOD_RCTRL)) != 0;
    bool alt   = (report->modifiers & (USB_HID_MOD_LALT | USB_HID_MOD_RALT)) != 0;

    out_event->shift = shift;
    out_event->ctrl = ctrl;
    out_event->alt = alt;

    /* Read first pressed keycode */
    uint8_t kc = report->keycodes[0];
    if (kc != 0) {
        out_event->keycode = kc;
        out_event->pressed = true;
        out_event->ascii = shift ? hid_shifted[kc] : hid_unshifted[kc];
    }
}

void usb_hid_process_mouse_report(const struct usb_hid_mouse_report *report, struct mouse_event *out_event) {
    if (report == NULL || out_event == NULL) return;

    hid_mouse_x += (int32_t)report->dx;
    hid_mouse_y += (int32_t)report->dy;

    if (hid_mouse_x < 0) hid_mouse_x = 0;
    if ((uint32_t)hid_mouse_x >= hid_bound_x) hid_mouse_x = (int32_t)(hid_bound_x - 1);
    if (hid_mouse_y < 0) hid_mouse_y = 0;
    if ((uint32_t)hid_mouse_y >= hid_bound_y) hid_mouse_y = (int32_t)(hid_bound_y - 1);

    out_event->x = hid_mouse_x;
    out_event->y = hid_mouse_y;
    out_event->dx = (int32_t)report->dx;
    out_event->dy = (int32_t)report->dy;
    out_event->left_button = (report->buttons & 0x01U) != 0;
    out_event->right_button = (report->buttons & 0x02U) != 0;
    out_event->middle_button = (report->buttons & 0x04U) != 0;
}

/* Self-test */
bool usb_hid_self_test(void) {
    /* Test keyboard report parsing */
    struct usb_hid_keyboard_report krep = {0};
    krep.modifiers = USB_HID_MOD_LSHIFT;
    krep.keycodes[0] = 0x04; /* Usage 0x04 = 'a' */

    struct key_event kevt = {0};
    usb_hid_process_keyboard_report(&krep, &kevt);
    if (!kevt.pressed || !kevt.shift || kevt.ascii != 'A') return false;

    /* Unshifted key */
    krep.modifiers = 0;
    krep.keycodes[0] = 0x1E; /* Usage 0x1E = '1' */
    usb_hid_process_keyboard_report(&krep, &kevt);
    if (!kevt.pressed || kevt.shift || kevt.ascii != '1') return false;

    /* Test mouse report parsing */
    hid_mouse_x = 100;
    hid_mouse_y = 100;

    struct usb_hid_mouse_report mrep = {0};
    mrep.buttons = 0x01; /* Left click */
    mrep.dx = 25;
    mrep.dy = -15;

    struct mouse_event mevt = {0};
    usb_hid_process_mouse_report(&mrep, &mevt);
    if (!mevt.left_button || mevt.right_button) return false;
    if (mevt.dx != 25 || mevt.dy != -15) return false;
    if (mevt.x != 125 || mevt.y != 85) return false;

    return true;
}
