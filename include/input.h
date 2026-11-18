/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_INPUT_H
#define DAWN_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ps2.h"
#include "usb_hid.h"

#define INPUT_QUEUE_CAPACITY 64U

enum input_event_type {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEYBOARD = 1,
    INPUT_EVENT_MOUSE = 2
};

struct input_event {
    enum input_event_type type;
    union {
        struct key_event key;
        struct mouse_event mouse;
    };
};

bool input_initialize(void);
bool input_push_key_event(const struct key_event *evt);
bool input_push_mouse_event(const struct mouse_event *evt);
bool input_poll_event(struct input_event *out_event);
bool input_has_event(void);
void input_poll_hardware(void);

bool input_self_test(void);

#endif /* DAWN_INPUT_H */
