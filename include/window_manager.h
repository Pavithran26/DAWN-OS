/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_WINDOW_MANAGER_H
#define DAWN_WINDOW_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "input.h"

#define WM_MAX_WINDOWS      16U
#define WM_TITLEBAR_HEIGHT  28U
#define WM_BORDER_WIDTH     2U
#define WM_TITLE_MAX_LEN    32U

/* Window Flags */
#define WM_FLAG_NONE       0x00U
#define WM_FLAG_RESIZABLE  0x01U
#define WM_FLAG_MOVABLE    0x02U
#define WM_FLAG_TITLEBAR   0x04U
#define WM_FLAG_NO_CONTROLS 0x08U

struct window {
    uint32_t id;
    char title[WM_TITLE_MAX_LEN];
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t min_width;
    uint32_t min_height;
    uint32_t flags;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    int32_t saved_x;
    int32_t saved_y;
    uint32_t saved_width;
    uint32_t saved_height;
    bool dragging;
    int32_t drag_dx;
    int32_t drag_dy;
    uint32_t *client_buffer;
    uint32_t client_pitch;
    void (*on_click)(int32_t local_x, int32_t local_y);
    void (*on_close)(struct window *win);
    void (*on_key)(struct window *win, const struct key_event *kevt);
    bool active;
};

bool wm_initialize(void);
struct window *wm_create_window(const char *title, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t flags);
int wm_destroy_window(uint32_t id);
int wm_focus_window(uint32_t id);
int wm_cycle_focus(bool forward);
int wm_move_window(uint32_t id, int32_t x, int32_t y);
int wm_resize_window(uint32_t id, uint32_t w, uint32_t h);
int wm_minimize_window(uint32_t id);
int wm_maximize_window(uint32_t id);

struct window *wm_get_window_by_id(uint32_t id);
struct window *wm_find_window_by_title(const char *title);
struct window *wm_get_top_window(void);
struct window *wm_get_window_at_z(size_t index);
struct window *wm_find_window_at(int32_t x, int32_t y);
size_t wm_get_window_count(void);

void wm_handle_mouse(const struct mouse_event *mevt);
void wm_handle_key(const struct key_event *kevt);
void wm_composite(struct gfx_surface *surf, int32_t cursor_x, int32_t cursor_y);

bool wm_self_test(void);

#endif /* DAWN_WINDOW_MANAGER_H */
