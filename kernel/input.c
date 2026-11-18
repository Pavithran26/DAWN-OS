/* SPDX-License-Identifier: Unlicense */
#include "input.h"

static struct input_event event_queue[INPUT_QUEUE_CAPACITY];
static uint32_t queue_head = 0;
static uint32_t queue_tail = 0;
static uint32_t queue_count = 0;
static bool input_initialized = false;

bool input_initialize(void) {
    if (input_initialized) return true;
    for (size_t i = 0; i < INPUT_QUEUE_CAPACITY; i++) {
        event_queue[i] = (struct input_event){0};
    }
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;

    ps2_initialize();
    xhci_initialize();
    usb_hid_initialize();

    input_initialized = true;
    return true;
}

bool input_push_key_event(const struct key_event *evt) {
    if (evt == NULL || queue_count >= INPUT_QUEUE_CAPACITY) return false;
    struct input_event *slot = &event_queue[queue_tail];
    slot->type = INPUT_EVENT_KEYBOARD;
    slot->key = *evt;
    queue_tail = (queue_tail + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count++;
    return true;
}

bool input_push_mouse_event(const struct mouse_event *evt) {
    if (evt == NULL || queue_count >= INPUT_QUEUE_CAPACITY) return false;
    struct input_event *slot = &event_queue[queue_tail];
    slot->type = INPUT_EVENT_MOUSE;
    slot->mouse = *evt;
    queue_tail = (queue_tail + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count++;
    return true;
}

bool input_poll_event(struct input_event *out_event) {
    if (out_event == NULL || queue_count == 0) return false;
    *out_event = event_queue[queue_head];
    queue_head = (queue_head + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count--;
    return true;
}

bool input_has_event(void) {
    return (queue_count > 0);
}

void input_poll_hardware(void) {
    /* Poll PS/2 fallback */
    struct key_event kevt = {0};
    if (ps2_poll_key(&kevt)) {
        input_push_key_event(&kevt);
    }

    struct mouse_event mevt = {0};
    if (ps2_poll_mouse(&mevt)) {
        input_push_mouse_event(&mevt);
    }
}

/* Self-test */
bool input_self_test(void) {
    if (!input_initialize()) return false;

    /* Push key event */
    struct key_event kevt = {
        .scancode = 0x1E,
        .keycode = 0x1E,
        .ascii = 'a',
        .pressed = true,
        .shift = false,
        .ctrl = false,
        .alt = false
    };
    if (!input_push_key_event(&kevt)) return false;

    /* Push mouse event */
    struct mouse_event mevt = {
        .x = 200,
        .y = 150,
        .dx = 5,
        .dy = -3,
        .left_button = true,
        .right_button = false,
        .middle_button = false
    };
    if (!input_push_mouse_event(&mevt)) return false;

    if (!input_has_event()) return false;

    /* Dequeue key event */
    struct input_event ev1 = {0};
    if (!input_poll_event(&ev1)) return false;
    if (ev1.type != INPUT_EVENT_KEYBOARD) return false;
    if (ev1.key.ascii != 'a' || !ev1.key.pressed) return false;

    /* Dequeue mouse event */
    struct input_event ev2 = {0};
    if (!input_poll_event(&ev2)) return false;
    if (ev2.type != INPUT_EVENT_MOUSE) return false;
    if (ev2.mouse.x != 200 || ev2.mouse.y != 150 || !ev2.mouse.left_button) return false;

    /* Queue should now be empty */
    if (input_has_event()) return false;

    return true;
}
