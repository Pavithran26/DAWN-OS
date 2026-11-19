/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USER_GUI_H
#define DAWN_USER_GUI_H

#include "dawn.h"

/*
 * DAWN Emerald Neo-Liquid Glass Client-Server Display Protocol
 */

#define DAWN_GUI_COLOR_EMERALD_BASE      0xFF021B14U
#define DAWN_GUI_COLOR_EMERALD_PRIMARY   0xFF064E3BU
#define DAWN_GUI_COLOR_EMERALD_ACCENT    0xFF10B981U
#define DAWN_GUI_COLOR_EMERALD_HIGHLIGHT 0xFF34D399U
#define DAWN_GUI_COLOR_GLASS_PANEL       0xCC052E24U
#define DAWN_GUI_COLOR_TEXT_PRIMARY      0xFFECFDF5U
#define DAWN_GUI_COLOR_TEXT_MUTED        0xFF6EE7B7U

enum dawn_gui_event_type {
    DAWN_GUI_EVENT_NONE = 0,
    DAWN_GUI_EVENT_MOUSE_DOWN,
    DAWN_GUI_EVENT_MOUSE_UP,
    DAWN_GUI_EVENT_MOUSE_MOVE,
    DAWN_GUI_EVENT_KEY_DOWN,
    DAWN_GUI_EVENT_CLOSE
};

struct dawn_gui_event {
    enum dawn_gui_event_type type;
    int32_t x;
    int32_t y;
    char key_char;
};

typedef struct dawn_gui_window {
    uint64_t id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t *buffer;
    bool active;
} *dawn_window_t;

/* Client-side GUI APIs */
dawn_window_t dawn_gui_create_window(const char *title, int32_t x, int32_t y, uint32_t width, uint32_t height);
void dawn_gui_destroy_window(dawn_window_t win);
void dawn_gui_clear(dawn_window_t win, uint32_t color);
void dawn_gui_draw_rect(dawn_window_t win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void dawn_gui_draw_text(dawn_window_t win, int32_t x, int32_t y, const char *text, uint32_t color);
void dawn_gui_commit(dawn_window_t win);
bool dawn_gui_poll_event(dawn_window_t win, struct dawn_gui_event *out_event);

#endif
