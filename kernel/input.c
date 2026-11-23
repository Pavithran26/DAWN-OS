/* SPDX-License-Identifier: Unlicense */
#include "input.h"

static struct input_event event_queue[INPUT_QUEUE_CAPACITY];
static uint32_t queue_head = 0U;
static uint32_t queue_tail = 0U;
static uint32_t queue_count = 0U;
static bool input_initialized = false;

static uint64_t total_events = 0ULL;
static uint64_t dropped_events = 0ULL;
static uint64_t overflow_events = 0ULL;

static inline uint64_t cpu_save_flags_and_cli(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void cpu_restore_flags(uint64_t flags) {
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
}

bool input_initialize(void) {
    if (input_initialized) return true;

    for (size_t i = 0; i < INPUT_QUEUE_CAPACITY; i++) {
        event_queue[i] = (struct input_event){0};
    }
    queue_head = 0U;
    queue_tail = 0U;
    queue_count = 0U;
    total_events = 0ULL;
    dropped_events = 0ULL;
    overflow_events = 0ULL;

    (void)ps2_initialize();
    (void)xhci_initialize();
    (void)usb_hid_initialize();

    input_initialized = true;
    return true;
}

bool input_push_key_event(const struct key_event *evt) {
    if (evt == NULL) return false;
    uint64_t flags = cpu_save_flags_and_cli();

    if (queue_count >= INPUT_QUEUE_CAPACITY) {
        dropped_events++;
        overflow_events++;
        cpu_restore_flags(flags);
        return false;
    }

    struct input_event *slot = &event_queue[queue_tail];
    slot->type = INPUT_EVENT_KEYBOARD;
    slot->key = *evt;
    queue_tail = (queue_tail + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count++;
    total_events++;

    cpu_restore_flags(flags);
    return true;
}

bool input_push_mouse_event(const struct mouse_event *evt) {
    if (evt == NULL) return false;
    uint64_t flags = cpu_save_flags_and_cli();

    /* Coalesce consecutive motion events with identical button states to prevent queue congestion */
    if (queue_count > 0U) {
        uint32_t prev_idx = (queue_tail + INPUT_QUEUE_CAPACITY - 1U) % INPUT_QUEUE_CAPACITY;
        struct input_event *prev = &event_queue[prev_idx];
        if (prev->type == INPUT_EVENT_MOUSE &&
            prev->mouse.left_button == evt->left_button &&
            prev->mouse.right_button == evt->right_button &&
            prev->mouse.middle_button == evt->middle_button) {
            prev->mouse.x = evt->x;
            prev->mouse.y = evt->y;
            prev->mouse.dx += evt->dx;
            prev->mouse.dy += evt->dy;
            total_events++;
            cpu_restore_flags(flags);
            return true;
        }
    }

    if (queue_count >= INPUT_QUEUE_CAPACITY) {
        dropped_events++;
        overflow_events++;
        cpu_restore_flags(flags);
        return false;
    }

    struct input_event *slot = &event_queue[queue_tail];
    slot->type = INPUT_EVENT_MOUSE;
    slot->mouse = *evt;
    queue_tail = (queue_tail + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count++;
    total_events++;

    cpu_restore_flags(flags);
    return true;
}

bool input_poll_event(struct input_event *out_event) {
    if (out_event == NULL) return false;
    uint64_t flags = cpu_save_flags_and_cli();

    if (queue_count == 0U) {
        cpu_restore_flags(flags);
        return false;
    }

    *out_event = event_queue[queue_head];
    queue_head = (queue_head + 1U) % INPUT_QUEUE_CAPACITY;
    queue_count--;

    cpu_restore_flags(flags);
    return true;
}

bool input_has_event(void) {
    uint64_t flags = cpu_save_flags_and_cli();
    bool has = (queue_count > 0U);
    cpu_restore_flags(flags);
    return has;
}

void input_clear_queue(void) {
    uint64_t flags = cpu_save_flags_and_cli();
    queue_head = 0U;
    queue_tail = 0U;
    queue_count = 0U;
    cpu_restore_flags(flags);
}

uint64_t input_get_total_events(void) { return total_events; }
uint64_t input_get_dropped_events(void) { return dropped_events; }
uint64_t input_get_overflow_count(void) { return overflow_events; }
uint32_t input_get_queue_count(void) {
    uint64_t flags = cpu_save_flags_and_cli();
    uint32_t count = queue_count;
    cpu_restore_flags(flags);
    return count;
}
uint32_t input_get_queue_capacity(void) { return INPUT_QUEUE_CAPACITY; }

void input_poll_hardware(void) {
    /* Poll PS/2 fallback */
    struct key_event kevt = {0};
    if (ps2_poll_key(&kevt)) {
        (void)input_push_key_event(&kevt);
    }

    struct mouse_event mevt = {0};
    if (ps2_poll_mouse(&mevt)) {
        (void)input_push_mouse_event(&mevt);
    }
}

/* Self-test */
bool input_self_test(void) {
    if (!input_initialize()) return false;
    input_clear_queue();

    /* Test 1: Push key event and mouse event, verify FIFO order */
    struct key_event kevt = {
        .scancode = 0x1EU,
        .keycode = 0x1EU,
        .ascii = 'a',
        .pressed = true,
        .shift = false,
        .ctrl = false,
        .alt = false,
        .gui = false
    };
    if (!input_push_key_event(&kevt)) return false;

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
    if (input_get_queue_count() != 2U) return false;

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

    /* Test 2: Mouse Motion Coalescing */
    struct mouse_event move1 = { .x = 10, .y = 10, .dx = 2, .dy = 3, .left_button = false, .right_button = false, .middle_button = false };
    struct mouse_event move2 = { .x = 15, .y = 14, .dx = 5, .dy = 4, .left_button = false, .right_button = false, .middle_button = false };
    if (!input_push_mouse_event(&move1)) return false;
    if (!input_push_mouse_event(&move2)) return false;

    /* Should coalesce into single slot */
    if (input_get_queue_count() != 1U) return false;
    struct input_event coal_ev = {0};
    if (!input_poll_event(&coal_ev)) return false;
    if (coal_ev.mouse.x != 15 || coal_ev.mouse.y != 14 || coal_ev.mouse.dx != 7 || coal_ev.mouse.dy != 7) return false;

    /* Test 3: Bounded Queue Overflow Protection */
    input_clear_queue();
    uint64_t before_overflow = input_get_overflow_count();

    /* Fill exactly to capacity with distinct key events */
    for (uint32_t i = 0U; i < INPUT_QUEUE_CAPACITY; i++) {
        struct key_event k = {
            .scancode = (uint8_t)(i & 0x7FU),
            .keycode = (uint8_t)(i & 0x7FU),
            .ascii = 'k',
            .pressed = true,
            .shift = false,
            .ctrl = false,
            .alt = false,
            .gui = false
        };
        if (!input_push_key_event(&k)) return false;
    }
    if (input_get_queue_count() != INPUT_QUEUE_CAPACITY) return false;

    /* Push 5 extra events beyond capacity: all must be rejected */
    for (uint32_t i = 0U; i < 5U; i++) {
        struct key_event k_extra = { .scancode = 0x55U, .keycode = 0x55U, .pressed = true, .ascii = 'x', .shift = false, .ctrl = false, .alt = false, .gui = false };
        if (input_push_key_event(&k_extra)) return false; /* Must fail */
    }

    if (input_get_overflow_count() != before_overflow + 5ULL) return false;
    if (input_get_queue_count() != INPUT_QUEUE_CAPACITY) return false;

    /* Drain all capacity items and verify integrity and order */
    for (uint32_t i = 0U; i < INPUT_QUEUE_CAPACITY; i++) {
        struct input_event ev = {0};
        if (!input_poll_event(&ev)) return false;
        if (ev.type != INPUT_EVENT_KEYBOARD) return false;
        if (ev.key.scancode != (uint8_t)(i & 0x7FU)) return false;
    }

    /* Queue must be empty now */
    if (input_has_event()) return false;
    if (input_get_queue_count() != 0U) return false;

    return true;
}
