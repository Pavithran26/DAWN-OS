/* SPDX-License-Identifier: Unlicense */
#include "ps2.h"
#include "device.h"

static bool has_kbd = false;
static bool has_mouse = false;
static bool lshift_down = false;
static bool rshift_down = false;
static bool ctrl_down = false;
static bool alt_down = false;
static bool gui_down = false;
static bool capslock_on = false;
static bool extended_prefix = false;

static int32_t mouse_x = 512;
static int32_t mouse_y = 384;
static uint32_t mouse_bound_x = 1024U;
static uint32_t mouse_bound_y = 768U;

static uint8_t mouse_cycle = 0U;
static uint8_t mouse_packet_buf[3];

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ps2_wait_write(void) {
    uint32_t timeout = 10000U;
    while (timeout > 0U) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if (status == 0xFFU) break; /* Absent device / floating bus */
        if ((status & PS2_STATUS_INPUT_FULL) == 0U) break;
        timeout--;
    }
}

static void ps2_wait_read(void) {
    uint32_t timeout = 10000U;
    while (timeout > 0U) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if (status == 0xFFU) break; /* Absent device / floating bus */
        if ((status & PS2_STATUS_OUTPUT_FULL) != 0U) break;
        timeout--;
    }
}

static const char kbd_unshifted[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char kbd_shifted[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void ps2_reset_state(void) {
    lshift_down = false;
    rshift_down = false;
    ctrl_down = false;
    alt_down = false;
    gui_down = false;
    capslock_on = false;
    extended_prefix = false;
    mouse_cycle = 0U;
}

void ps2_handle_keyboard_scancode(uint8_t scancode, struct key_event *out_event) {
    if (out_event == NULL) return;
    *out_event = (struct key_event){0};

    /* Handle extended prefix 0xE0 */
    if (scancode == 0xE0U) {
        extended_prefix = true;
        return;
    }

    bool released = (scancode & 0x80U) != 0U;
    uint8_t make_code = (uint8_t)(scancode & 0x7FU);

    if (extended_prefix) {
        extended_prefix = false;
        switch (make_code) {
            case 0x48U: make_code = KEY_UP; break;
            case 0x50U: make_code = KEY_DOWN; break;
            case 0x4BU: make_code = KEY_LEFT; break;
            case 0x4DU: make_code = KEY_RIGHT; break;
            case 0x5BU: make_code = KEY_SUPER; break;
            case 0x53U: make_code = KEY_DELETE; break;
            case 0x1DU: ctrl_down = !released; break; /* Right Ctrl */
            case 0x38U: alt_down = !released; break;  /* Right Alt */
            default: break;
        }
    } else {
        if (make_code == KEY_LSHIFT) lshift_down = !released;
        if (make_code == KEY_RSHIFT) rshift_down = !released;
        if (make_code == KEY_LCTRL)  ctrl_down = !released;
        if (make_code == KEY_LALT)   alt_down = !released;
        if (make_code == KEY_CAPSLOCK && !released) capslock_on = !capslock_on;
    }

    if (make_code == KEY_SUPER) {
        gui_down = !released;
    }

    out_event->scancode = scancode;
    out_event->keycode = make_code;
    out_event->pressed = !released;

    bool shift_active = (lshift_down || rshift_down);
    if (capslock_on && make_code >= 0x10U && make_code <= 0x32U) {
        shift_active = !shift_active;
    }

    out_event->shift = shift_active;
    out_event->ctrl = ctrl_down;
    out_event->alt = alt_down;
    out_event->gui = gui_down;

    if (make_code < 128U) {
        out_event->ascii = shift_active ? kbd_shifted[make_code] : kbd_unshifted[make_code];
    }
}

void ps2_set_mouse_bounds(uint32_t max_x, uint32_t max_y) {
    mouse_bound_x = (max_x > 0U) ? max_x : 1024U;
    mouse_bound_y = (max_y > 0U) ? max_y : 768U;
}

bool ps2_handle_mouse_packet(const uint8_t packet[3], struct mouse_event *out_event) {
    if (packet == NULL || out_event == NULL) return false;

    /* Bit 3 of byte 0 must be 1 (sync bit) */
    if ((packet[0] & 0x08U) == 0U) return false;

    /* Reject packet if X or Y overflow bits are set (bits 6 and 7) */
    if ((packet[0] & 0xC0U) != 0U) return false;

    int32_t dx = (int32_t)packet[1];
    int32_t dy = (int32_t)packet[2];

    if ((packet[0] & 0x10U) != 0U) dx |= (int32_t)0xFFFFFF00; /* Sign extend X */
    if ((packet[0] & 0x20U) != 0U) dy |= (int32_t)0xFFFFFF00; /* Sign extend Y */

    mouse_x += dx;
    mouse_y -= dy; /* PS/2 Y axis is inverted */

    if (mouse_x < 0) mouse_x = 0;
    if ((uint32_t)mouse_x >= mouse_bound_x) mouse_x = (int32_t)(mouse_bound_x - 1U);
    if (mouse_y < 0) mouse_y = 0;
    if ((uint32_t)mouse_y >= mouse_bound_y) mouse_y = (int32_t)(mouse_bound_y - 1U);

    out_event->x = mouse_x;
    out_event->y = mouse_y;
    out_event->dx = dx;
    out_event->dy = dy;
    out_event->left_button = (packet[0] & 0x01U) != 0U;
    out_event->right_button = (packet[0] & 0x02U) != 0U;
    out_event->middle_button = (packet[0] & 0x04U) != 0U;

    return true;
}

bool ps2_initialize(void) {
    ps2_reset_state();

    /* 8042 controller initialization */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAD); /* Disable port 1 */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xA7); /* Disable port 2 */

    /* Flush output buffer */
    uint32_t flush_count = 64U;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && flush_count > 0U) {
        (void)inb(PS2_DATA_PORT);
        flush_count--;
    }

    /* Read controller configuration */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA_PORT);
    if (config != 0xFFU) {
        config |= 0x43U; /* Enable interrupts for port 1 and 2, keep translation enabled (Set 1) */

        /* Write back config */
        ps2_wait_write();
        outb(PS2_COMMAND_PORT, 0x60);
        ps2_wait_write();
        outb(PS2_DATA_PORT, config);

        /* Enable Port 1 (Keyboard) */
        ps2_wait_write();
        outb(PS2_COMMAND_PORT, 0xAE);
        has_kbd = true;

        /* Enable Port 2 (Mouse) */
        ps2_wait_write();
        outb(PS2_COMMAND_PORT, 0xA8);
        has_mouse = true;
    } else {
        has_kbd = false;
        has_mouse = false;
    }

    /* Register devices in Device Manager */
    struct device kbd_dev = {0};
    kbd_dev.bus = BUS_TYPE_PS2;
    kbd_dev.class_type = DEV_CLASS_INPUT;
    kbd_dev.resource.io_port_base = PS2_DATA_PORT;
    kbd_dev.resource.io_port_count = 5U;
    kbd_dev.resource.irq_line = 1U;
    kbd_dev.name[0] = 'k'; kbd_dev.name[1] = 'b'; kbd_dev.name[2] = 'd'; kbd_dev.name[3] = '0';
    device_register(&kbd_dev);

    struct device mouse_dev = {0};
    mouse_dev.bus = BUS_TYPE_PS2;
    mouse_dev.class_type = DEV_CLASS_INPUT;
    mouse_dev.resource.io_port_base = PS2_DATA_PORT;
    mouse_dev.resource.io_port_count = 5U;
    mouse_dev.resource.irq_line = 12U;
    mouse_dev.name[0] = 'm'; mouse_dev.name[1] = 'o'; mouse_dev.name[2] = 'u'; mouse_dev.name[3] = 's'; mouse_dev.name[4] = 'e'; mouse_dev.name[5] = '0';
    device_register(&mouse_dev);

    return true;
}

bool ps2_has_keyboard(void) { return has_kbd; }
bool ps2_has_mouse(void) { return has_mouse; }

bool ps2_poll_key(struct key_event *out_event) {
    if (out_event == NULL) return false;
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status == 0xFFU) return false; /* Absent device / floating bus */
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0U || (status & PS2_STATUS_AUX_DATA) != 0U) return false;
    uint8_t scancode = inb(PS2_DATA_PORT);
    ps2_handle_keyboard_scancode(scancode, out_event);
    return (out_event->scancode != 0U);
}

bool ps2_poll_mouse(struct mouse_event *out_event) {
    if (out_event == NULL) return false;
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status == 0xFFU) return false; /* Absent device / floating bus */
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0U || (status & PS2_STATUS_AUX_DATA) == 0U) return false;
    uint8_t byte = inb(PS2_DATA_PORT);

    if (mouse_cycle == 0U) {
        if ((byte & 0x08U) == 0U) return false; /* Sync bit missing */
        mouse_packet_buf[0] = byte;
        mouse_cycle = 1U;
        return false;
    } else if (mouse_cycle == 1U) {
        mouse_packet_buf[1] = byte;
        mouse_cycle = 2U;
        return false;
    } else {
        mouse_packet_buf[2] = byte;
        mouse_cycle = 0U;
        return ps2_handle_mouse_packet(mouse_packet_buf, out_event);
    }
}

/* Self-test */
bool ps2_self_test(void) {
    ps2_reset_state();

    /* Test 1: Basic key decoding */
    struct key_event evt = {0};

    /* Key 'A' press (scancode 0x1E) unshifted */
    ps2_handle_keyboard_scancode(0x1E, &evt);
    if (!evt.pressed || evt.ascii != 'a' || evt.keycode != 0x1E) return false;

    /* Key 'A' release (scancode 0x9E) */
    ps2_handle_keyboard_scancode(0x9E, &evt);
    if (evt.pressed) return false;

    /* Left Shift press (scancode 0x2A) */
    ps2_handle_keyboard_scancode(0x2A, &evt);
    if (!evt.shift) return false;

    /* Key 'A' press shifted -> 'A' */
    ps2_handle_keyboard_scancode(0x1E, &evt);
    if (!evt.pressed || evt.ascii != 'A' || !evt.shift) return false;

    /* Left Shift release (scancode 0xAA) */
    ps2_handle_keyboard_scancode(0xAA, &evt);
    if (evt.shift) return false;

    /* Test 2: Extended keys (0xE0 prefix) */
    /* Arrow Up: 0xE0 followed by 0x48 */
    ps2_handle_keyboard_scancode(0xE0, &evt);
    ps2_handle_keyboard_scancode(0x48, &evt);
    if (!evt.pressed || evt.keycode != KEY_UP) return false;

    /* Super / Windows key: 0xE0 followed by 0x5B */
    ps2_handle_keyboard_scancode(0xE0, &evt);
    ps2_handle_keyboard_scancode(0x5B, &evt);
    if (!evt.pressed || evt.keycode != KEY_SUPER || !evt.gui) return false;

    /* Super release: 0xE0 followed by 0xDB */
    ps2_handle_keyboard_scancode(0xE0, &evt);
    ps2_handle_keyboard_scancode(0xDB, &evt);
    if (evt.pressed || evt.gui) return false;

    /* Test 3: Mouse packet handling */
    ps2_set_mouse_bounds(800U, 600U);
    mouse_x = 400;
    mouse_y = 300;

    /* Move +10 in X, +5 in Y, Left button down */
    uint8_t test_packet[3] = {0x09U, 10, 5}; /* Bit 0 (Left), Bit 3 (Sync 1) */
    struct mouse_event mevt = {0};
    if (!ps2_handle_mouse_packet(test_packet, &mevt)) return false;
    if (!mevt.left_button || mevt.right_button || mevt.middle_button) return false;
    if (mevt.dx != 10 || mevt.dy != 5) return false;
    if (mevt.x != 410 || mevt.y != 295) return false;

    /* Right & middle button press */
    uint8_t btn_packet[3] = {0x0EU, 0, 0}; /* Bit 1 (Right), Bit 2 (Middle), Bit 3 (Sync) */
    if (!ps2_handle_mouse_packet(btn_packet, &mevt)) return false;
    if (mevt.left_button || !mevt.right_button || !mevt.middle_button) return false;

    /* Bad sync bit rejection */
    uint8_t bad_sync[3] = {0x01U, 0, 0}; /* Bit 3 is 0 */
    if (ps2_handle_mouse_packet(bad_sync, &mevt)) return false;

    /* Overflow packet rejection */
    uint8_t overflow_packet[3] = {0x48U, 100, 0}; /* Bit 6 set */
    if (ps2_handle_mouse_packet(overflow_packet, &mevt)) return false;

    /* Boundary clipping test */
    mouse_x = 795;
    uint8_t big_move[3] = {0x08U, 100, 0};
    if (!ps2_handle_mouse_packet(big_move, &mevt)) return false;
    if (mevt.x != 799) return false; /* Clamped to 800 - 1 */

    return true;
}
