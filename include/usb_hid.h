/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USB_HID_H
#define DAWN_USB_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ps2.h" /* For key_event and mouse_event */

#define USB_HID_BOOT_PROTOCOL_KEYBOARD 1U
#define USB_HID_BOOT_PROTOCOL_MOUSE    2U

#define USB_HID_MOD_LCTRL  0x01U
#define USB_HID_MOD_LSHIFT 0x02U
#define USB_HID_MOD_LALT   0x04U
#define USB_HID_MOD_LGUI   0x08U
#define USB_HID_MOD_RCTRL  0x10U
#define USB_HID_MOD_RSHIFT 0x20U
#define USB_HID_MOD_RALT   0x40U
#define USB_HID_MOD_RGUI   0x80U

struct usb_hid_keyboard_report {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycodes[6];
} __attribute__((packed));

struct usb_hid_mouse_report {
    uint8_t buttons;
    int8_t  dx;
    int8_t  dy;
    int8_t  wheel;
} __attribute__((packed));

bool usb_hid_initialize(void);
void usb_hid_process_keyboard_report(const struct usb_hid_keyboard_report *report, struct key_event *out_event);
void usb_hid_process_mouse_report(const struct usb_hid_mouse_report *report, struct mouse_event *out_event);

bool usb_hid_self_test(void);

#endif /* DAWN_USB_HID_H */
