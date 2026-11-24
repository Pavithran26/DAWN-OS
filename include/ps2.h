/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PS2_H
#define DAWN_PS2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS2_DATA_PORT    0x60U
#define PS2_STATUS_PORT  0x64U
#define PS2_COMMAND_PORT 0x64U

#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL  0x02U
#define PS2_STATUS_AUX_DATA    0x20U

/* Key Codes */
#define KEY_UNKNOWN    0x00U
#define KEY_ESCAPE     0x01U
#define KEY_1          0x02U
#define KEY_2          0x03U
#define KEY_3          0x04U
#define KEY_4          0x05U
#define KEY_5          0x06U
#define KEY_6          0x07U
#define KEY_7          0x08U
#define KEY_8          0x09U
#define KEY_0          0x0BU
#define KEY_BACKSPACE  0x0EU
#define KEY_TAB        0x0FU
#define KEY_ENTER      0x1CU
#define KEY_LCTRL      0x1DU
#define KEY_LSHIFT     0x2AU
#define KEY_RSHIFT     0x36U
#define KEY_LALT       0x38U
#define KEY_SPACE      0x39U
#define KEY_CAPSLOCK   0x3AU
#define KEY_F1         0x3BU
#define KEY_F4         0x3EU
#define KEY_HOME       0x47U
#define KEY_UP         0x48U
#define KEY_PAGE_UP    0x49U
#define KEY_LEFT       0x4BU
#define KEY_RIGHT      0x4DU
#define KEY_END        0x4FU
#define KEY_DOWN       0x50U
#define KEY_PAGE_DOWN  0x51U
#define KEY_DELETE     0x53U
#define KEY_SUPER      0x5BU

struct key_event {
    uint8_t scancode;
    uint8_t keycode;
    char    ascii;
    bool    pressed;
    bool    shift;
    bool    ctrl;
    bool    alt;
    bool    gui;
};

struct mouse_event {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    bool    left_button;
    bool    right_button;
    bool    middle_button;
};

bool ps2_initialize(void);
bool ps2_has_keyboard(void);
bool ps2_has_mouse(void);
void ps2_reset_state(void);

/* Keyboard interface */
void ps2_handle_keyboard_scancode(uint8_t scancode, struct key_event *out_event);
bool ps2_poll_key(struct key_event *out_event);

/* Mouse interface */
void ps2_set_mouse_bounds(uint32_t max_x, uint32_t max_y);
bool ps2_handle_mouse_packet(const uint8_t packet[3], struct mouse_event *out_event);
bool ps2_poll_mouse(struct mouse_event *out_event);

bool ps2_self_test(void);

#endif /* DAWN_PS2_H */
