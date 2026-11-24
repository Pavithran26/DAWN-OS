/* SPDX-License-Identifier: Unlicense */
#include "window_manager.h"

static struct window window_pool[WM_MAX_WINDOWS];
static struct window *z_order[WM_MAX_WINDOWS];
static size_t z_count = 0;
static uint32_t next_win_id = 1;
static uint32_t current_workspace = 0U;
static bool wm_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

void wm_mark_window_damaged(const struct window *win) {
    if (win == NULL) return;
    int32_t x = win->x;
    int32_t y = win->y;
    uint32_t w = win->width + 8U;
    uint32_t h = win->height + 8U;
    if (x < 0) {
        if ((uint32_t)(-x) >= w) return;
        w -= (uint32_t)(-x);
        x = 0;
    }
    if (y < 0) {
        if ((uint32_t)(-y) >= h) return;
        h -= (uint32_t)(-y);
        y = 0;
    }
    gfx_damage_add((uint32_t)x, (uint32_t)y, w, h);
}

uint32_t wm_get_current_workspace(void) {
    return current_workspace;
}

int wm_set_current_workspace(uint32_t ws) {
    if (ws >= WM_MAX_WORKSPACES) return -1;
    if (current_workspace != ws) {
        current_workspace = ws;
        /* Defocus active window if it does not belong to the new workspace */
        struct window *top = wm_get_top_window();
        if (top != NULL && top->workspace != ws && !(top->flags & WM_FLAG_ALL_WORKSPACES)) {
            top->focused = false;
            /* Focus top window on the new workspace */
            for (size_t i = z_count; i > 0; i--) {
                struct window *candidate = z_order[i - 1];
                if (candidate->active && candidate->visible && !candidate->minimized &&
                    (candidate->workspace == ws || (candidate->flags & WM_FLAG_ALL_WORKSPACES))) {
                    wm_focus_window(candidate->id);
                    break;
                }
            }
        }
        gfx_damage_add_fullscreen();
    }
    return 0;
}

int wm_move_window_to_workspace(uint32_t id, uint32_t ws) {
    if (ws >= WM_MAX_WORKSPACES) return -1;
    struct window *win = wm_get_window_by_id(id);
    if (win == NULL) return -1;
    wm_mark_window_damaged(win);
    win->workspace = ws;
    gfx_damage_add_fullscreen();
    return 0;
}

bool wm_initialize(void) {
    if (wm_initialized) return true;
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++) {
        window_pool[i] = (struct window){0};
        z_order[i] = NULL;
    }
    z_count = 0;
    next_win_id = 1;
    current_workspace = 0U;
    wm_initialized = true;
    return true;
}

struct window *wm_create_window(const char *title, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t flags) {
    if (!wm_initialized) wm_initialize();
    if (z_count >= WM_MAX_WINDOWS) return NULL;

    for (size_t i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!window_pool[i].active) {
            struct window *win = &window_pool[i];
            *win = (struct window){0};
            win->id = next_win_id++;
            str_copy(win->title, title ? title : "Window", WM_TITLE_MAX_LEN);
            win->x = x;
            win->y = y;
            win->width = (w >= 100) ? w : 100;
            win->height = (h >= 60) ? h : 60;
            win->min_width = 100;
            win->min_height = 60;
            win->flags = flags | WM_FLAG_TITLEBAR | WM_FLAG_MOVABLE | WM_FLAG_RESIZABLE;
            win->workspace = current_workspace;
            win->visible = true;
            win->active = true;
            win->resizing = false;
            win->on_click = NULL;
            win->on_close = NULL;
            win->on_key = NULL;

            z_order[z_count++] = win;
            wm_focus_window(win->id);
            wm_mark_window_damaged(win);
            return win;
        }
    }
    return NULL;
}

int wm_destroy_window(uint32_t id) {
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++) {
        if (window_pool[i].active && window_pool[i].id == id) {
            wm_mark_window_damaged(&window_pool[i]);
            if (window_pool[i].on_close != NULL) {
                window_pool[i].on_close(&window_pool[i]);
            }
            window_pool[i].active = false;

            /* Remove from Z-order */
            size_t found_idx = WM_MAX_WINDOWS;
            for (size_t z = 0; z < z_count; z++) {
                if (z_order[z] == &window_pool[i]) {
                    found_idx = z;
                    break;
                }
            }
            if (found_idx < z_count) {
                for (size_t z = found_idx; z + 1 < z_count; z++) {
                    z_order[z] = z_order[z + 1];
                }
                z_order[z_count - 1] = NULL;
                z_count--;
            }

            if (z_count > 0) {
                /* Find top active window on current workspace to focus */
                struct window *top = wm_get_top_window();
                if (top != NULL) {
                    wm_focus_window(top->id);
                }
            }
            return 0;
        }
    }
    return -1;
}

int wm_focus_window(uint32_t id) {
    struct window *target = NULL;
    size_t z_idx = 0;

    for (size_t i = 0; i < z_count; i++) {
        if (z_order[i]->id == id) {
            target = z_order[i];
            z_idx = i;
            break;
        }
    }
    if (target == NULL) return -1;

    /* Move target to top of Z-order */
    for (size_t i = z_idx; i + 1 < z_count; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[z_count - 1] = target;

    for (size_t i = 0; i < z_count; i++) {
        z_order[i]->focused = (z_order[i] == target);
    }
    if (target->minimized) {
        target->minimized = false;
    }
    wm_mark_window_damaged(target);
    return 0;
}

int wm_cycle_focus(bool forward) {
    struct window *ws_windows[WM_MAX_WINDOWS];
    size_t count = 0;
    for (size_t i = 0; i < z_count; i++) {
        struct window *w = z_order[i];
        if (w != NULL && w->visible && !w->minimized &&
            (w->workspace == current_workspace || (w->flags & WM_FLAG_ALL_WORKSPACES))) {
            ws_windows[count++] = w;
        }
    }
    if (count <= 1U) return 0;
    if (forward) {
        return wm_focus_window(ws_windows[count - 2U]->id);
    } else {
        return wm_focus_window(ws_windows[0]->id);
    }
}

int wm_move_window(uint32_t id, int32_t x, int32_t y) {
    struct window *win = wm_get_window_by_id(id);
    if (win == NULL) return -1;
    wm_mark_window_damaged(win);
    win->x = x;
    win->y = y;
    wm_mark_window_damaged(win);
    return 0;
}

int wm_resize_window(uint32_t id, uint32_t w, uint32_t h) {
    struct window *win = wm_get_window_by_id(id);
    if (win == NULL) return -1;
    wm_mark_window_damaged(win);
    win->width = (w >= win->min_width) ? w : win->min_width;
    win->height = (h >= win->min_height) ? h : win->min_height;
    wm_mark_window_damaged(win);
    return 0;
}

int wm_minimize_window(uint32_t id) {
    struct window *win = wm_get_window_by_id(id);
    if (win == NULL) return -1;
    wm_mark_window_damaged(win);
    win->minimized = !win->minimized;
    if (!win->minimized) {
        wm_focus_window(id);
    }
    wm_mark_window_damaged(win);
    return 0;
}

int wm_maximize_window(uint32_t id) {
    struct window *win = wm_get_window_by_id(id);
    if (win == NULL) return -1;

    wm_mark_window_damaged(win);
    if (!win->maximized) {
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_width = win->width;
        win->saved_height = win->height;

        struct gfx_surface *fb = gfx_get_framebuffer();
        uint32_t sw = (fb && fb->width > 0) ? fb->width : 1024U;
        uint32_t sh = (fb && fb->height > 0) ? fb->height : 768U;

        win->x = 0;
        win->y = (int32_t)WM_TOP_BAR_HEIGHT;
        win->width = sw;
        uint32_t chrome_bars = WM_TOP_BAR_HEIGHT + 48U;
        win->height = (sh > chrome_bars) ? (sh - chrome_bars) : sh;
        win->maximized = true;
    } else {
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->width = win->saved_width;
        win->height = win->saved_height;
        win->maximized = false;
    }
    wm_mark_window_damaged(win);
    return 0;
}

struct window *wm_get_window_by_id(uint32_t id) {
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++) {
        if (window_pool[i].active && window_pool[i].id == id) {
            return &window_pool[i];
        }
    }
    return NULL;
}

struct window *wm_find_window_by_title(const char *title) {
    if (!wm_initialized || title == NULL) return NULL;
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++) {
        if (window_pool[i].active && str_equal(window_pool[i].title, title)) {
            return &window_pool[i];
        }
    }
    return NULL;
}

struct window *wm_get_top_window(void) {
    for (size_t i = z_count; i > 0; i--) {
        struct window *win = z_order[i - 1];
        if (win != NULL && win->visible && !win->minimized &&
            (win->workspace == current_workspace || (win->flags & WM_FLAG_ALL_WORKSPACES))) {
            return win;
        }
    }
    return NULL;
}

struct window *wm_get_window_at_z(size_t index) {
    if (index >= z_count) return NULL;
    return z_order[index];
}

struct window *wm_find_window_at(int32_t x, int32_t y) {
    for (size_t i = z_count; i > 0; i--) {
        struct window *win = z_order[i - 1];
        if (win != NULL && win->visible && !win->minimized &&
            (win->workspace == current_workspace || (win->flags & WM_FLAG_ALL_WORKSPACES))) {
            if (x >= win->x && x < win->x + (int32_t)win->width &&
                y >= win->y && y < win->y + (int32_t)win->height) {
                return win;
            }
        }
    }
    return NULL;
}

size_t wm_get_window_count(void) {
    return z_count;
}

void wm_handle_mouse(const struct mouse_event *mevt) {
    static bool prev_left_button = false;
    if (mevt == NULL || z_count == 0) {
        prev_left_button = false;
        return;
    }

    bool is_press_edge = mevt->left_button && !prev_left_button;
    prev_left_button = mevt->left_button;

    if (mevt->left_button) {
        /* Check if dragging or resizing already */
        struct window *top = wm_get_top_window();
        if (top != NULL && top->dragging) {
            wm_mark_window_damaged(top);
            top->x = mevt->x - top->drag_dx;
            top->y = mevt->y - top->drag_dy;
            wm_mark_window_damaged(top);
            return;
        }
        if (top != NULL && top->resizing) {
            wm_mark_window_damaged(top);
            int32_t new_w = (mevt->x + top->resize_dx) - top->x;
            int32_t new_h = (mevt->y + top->resize_dy) - top->y;
            top->width = (new_w >= (int32_t)top->min_width) ? (uint32_t)new_w : top->min_width;
            top->height = (new_h >= (int32_t)top->min_height) ? (uint32_t)new_h : top->min_height;
            wm_mark_window_damaged(top);
            return;
        }

        if (!is_press_edge) {
            /* Held-down mouse button: do not repeat clicks or window actions */
            return;
        }

        /* Hit test from top to bottom on current workspace */
        for (size_t i = z_count; i > 0; i--) {
            struct window *win = z_order[i - 1];
            if (!win->visible || win->minimized) continue;
            if (win->workspace != current_workspace && !(win->flags & WM_FLAG_ALL_WORKSPACES)) continue;

            if (mevt->x >= win->x && (uint32_t)mevt->x < (uint32_t)(win->x + (int32_t)win->width) &&
                mevt->y >= win->y && (uint32_t)mevt->y < (uint32_t)(win->y + (int32_t)win->height)) {

                wm_focus_window(win->id);

                /* Check bottom-right resize grip */
                if ((win->flags & WM_FLAG_RESIZABLE) && !win->maximized &&
                    mevt->x >= (int32_t)(win->x + (int32_t)win->width - 16) &&
                    mevt->x <= (int32_t)(win->x + (int32_t)win->width) &&
                    mevt->y >= (int32_t)(win->y + (int32_t)win->height - 16) &&
                    mevt->y <= (int32_t)(win->y + (int32_t)win->height)) {
                    win->resizing = true;
                    win->resize_dx = (win->x + (int32_t)win->width) - mevt->x;
                    win->resize_dy = (win->y + (int32_t)win->height) - mevt->y;
                    return;
                }

                /* Titlebar check */
                if (mevt->y < win->y + (int32_t)WM_TITLEBAR_HEIGHT) {
                    int32_t rel_x = mevt->x - win->x;
                    /* Close button: width - 24 to width - 10 */
                    if (rel_x >= (int32_t)(win->width - 24) && rel_x <= (int32_t)(win->width - 10)) {
                        wm_destroy_window(win->id);
                        return;
                    }
                    /* Maximize button: width - 44 to width - 30 */
                    if (rel_x >= (int32_t)(win->width - 44) && rel_x <= (int32_t)(win->width - 30)) {
                        wm_maximize_window(win->id);
                        return;
                    }
                    /* Minimize button: width - 64 to width - 50 */
                    if (rel_x >= (int32_t)(win->width - 64) && rel_x <= (int32_t)(win->width - 50)) {
                        wm_minimize_window(win->id);
                        return;
                    }
                    /* Start dragging if movable */
                    if (win->flags & WM_FLAG_MOVABLE) {
                        win->dragging = true;
                        win->drag_dx = mevt->x - win->x;
                        win->drag_dy = mevt->y - win->y;
                    }
                } else {
                    /* Client area click dispatch */
                    int32_t cx = mevt->x - (win->x + (int32_t)WM_BORDER_WIDTH);
                    int32_t cy = mevt->y - (win->y + (int32_t)WM_TITLEBAR_HEIGHT);
                    if (win->on_click != NULL) {
                        win->on_click(cx, cy);
                    }
                }
                return;
            }
        }
    } else {
        /* Button released */
        for (size_t i = 0; i < z_count; i++) {
            z_order[i]->dragging = false;
            z_order[i]->resizing = false;
        }
    }
}

void wm_handle_key(const struct key_event *kevt) {
    if (kevt == NULL) return;

    if (kevt->pressed) {
        /* Alt + Tab: Window Focus Cycling */
        if (kevt->alt && kevt->keycode == KEY_TAB) {
            (void)wm_cycle_focus(!kevt->shift);
            return;
        }

        /* Alt + F4: Close Active Window */
        if (kevt->alt && kevt->keycode == KEY_F4) {
            struct window *top = wm_get_top_window();
            if (top != NULL) {
                (void)wm_destroy_window(top->id);
            }
            return;
        }
    }

    /* Pass key event to focused window callback if registered */
    struct window *top = wm_get_top_window();
    if (top != NULL && top->on_key != NULL) {
        top->on_key(top, kevt);
    }
}

void wm_composite(struct gfx_surface *surf, int32_t cursor_x, int32_t cursor_y) {
    if (surf == NULL) return;

    /* Render desktop background gradient (Emerald Forest to Deep Night) */
    gfx_draw_gradient_v(surf, 0, 0, surf->width, surf->height, GFX_COLOR_EMERALD_BASE, GFX_COLOR_EMERALD_DEEP);

    /* Render windows from back to front on current workspace */
    for (size_t z = 0; z < z_count; z++) {
        struct window *win = z_order[z];
        if (!win->visible || win->minimized) continue;
        if (win->workspace != current_workspace && !(win->flags & WM_FLAG_ALL_WORKSPACES)) continue;

        /* Ambient drop shadow */
        gfx_draw_rect_filled(surf, (uint32_t)(win->x + 4), (uint32_t)(win->y + 4), win->width, win->height, 0x50000000U);

        /* Window frame (frosted glass) */
        gfx_draw_glass_panel(surf, (uint32_t)win->x, (uint32_t)win->y, win->width, win->height, GFX_COLOR_GLASS_PANEL);

        /* Focus ring indication for active keyboard and window focus */
        if (win->focused) {
            gfx_draw_rect_outline(surf, (uint32_t)win->x, (uint32_t)win->y, win->width, win->height, GFX_COLOR_FOCUS_RING);
        }

        /* Titlebar */
        uint32_t tb_color = win->focused ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_BASE;
        gfx_draw_gradient_v(surf, (uint32_t)win->x + WM_BORDER_WIDTH, (uint32_t)win->y + WM_BORDER_WIDTH,
                            win->width - (2 * WM_BORDER_WIDTH), WM_TITLEBAR_HEIGHT - WM_BORDER_WIDTH,
                            tb_color, GFX_COLOR_EMERALD_DEEP);

        /* Title text */
        gfx_draw_string(surf, (uint32_t)(win->x + 12), (uint32_t)(win->y + 6), win->title,
                        win->focused ? GFX_COLOR_TEXT_PRIMARY : GFX_COLOR_TEXT_SECONDARY);

        /* Window Controls (Ruby, Amber, Emerald pills) */
        if (!(win->flags & WM_FLAG_NO_CONTROLS)) {
            uint32_t btn_y = (uint32_t)(win->y + 8);
            /* Close Button */
            gfx_draw_rect_filled(surf, (uint32_t)(win->x + (int32_t)win->width - 22), btn_y, 12, 12, GFX_COLOR_BTN_CLOSE);
            /* Maximize Button */
            gfx_draw_rect_filled(surf, (uint32_t)(win->x + (int32_t)win->width - 42), btn_y, 12, 12, GFX_COLOR_BTN_MAXIMIZE);
            /* Minimize Button */
            gfx_draw_rect_filled(surf, (uint32_t)(win->x + (int32_t)win->width - 62), btn_y, 12, 12, GFX_COLOR_BTN_MINIMIZE);
        }

        /* Client Area */
        uint32_t cx = (uint32_t)(win->x + (int32_t)WM_BORDER_WIDTH);
        uint32_t cy = (uint32_t)(win->y + (int32_t)WM_TITLEBAR_HEIGHT);
        uint32_t cw = win->width - (2 * WM_BORDER_WIDTH);
        uint32_t ch = win->height - WM_TITLEBAR_HEIGHT - (2 * WM_BORDER_WIDTH);

        if (win->client_buffer != NULL) {
            uint32_t max_buf_w = (win->client_width > 0U) ? win->client_width : (win->client_pitch / 4U);
            uint32_t max_buf_h = (win->client_height > 0U) ? win->client_height : ch;
            uint32_t render_w = (cw < max_buf_w) ? cw : max_buf_w;
            uint32_t render_h = (ch < max_buf_h) ? ch : max_buf_h;

            for (uint32_t r = 0; r < render_h; r++) {
                uint32_t *src_row = win->client_buffer + (r * (win->client_pitch / 4U));
                for (uint32_t c = 0; c < render_w; c++) {
                    uint32_t pixel = src_row[c];
                    uint32_t a = (pixel >> 24) & 0xFFU;
                    if (a == 255U) {
                        gfx_put_pixel(surf, cx + c, cy + r, pixel);
                    } else if (a > 0U) {
                        gfx_blend_pixel(surf, cx + c, cy + r, pixel);
                    }
                }
            }
        } else {
            gfx_draw_rect_filled(surf, cx, cy, cw, ch, GFX_COLOR_EMERALD_SURFACE);
        }

        /* Draw Resize Grip on bottom-right corner if resizable and not maximized */
        if ((win->flags & WM_FLAG_RESIZABLE) && !win->maximized) {
            uint32_t rx = (uint32_t)(win->x + (int32_t)win->width - 12);
            uint32_t ry = (uint32_t)(win->y + (int32_t)win->height - 12);
            uint32_t grip_col = win->focused ? GFX_COLOR_EMERALD_ACCENT : GFX_COLOR_TEXT_MUTED;
            gfx_draw_line(surf, (int32_t)rx + 8, (int32_t)ry + 10, (int32_t)rx + 10, (int32_t)ry + 8, grip_col);
            gfx_draw_line(surf, (int32_t)rx + 4, (int32_t)ry + 10, (int32_t)rx + 10, (int32_t)ry + 4, grip_col);
            gfx_draw_line(surf, (int32_t)rx,     (int32_t)ry + 10, (int32_t)rx + 10, (int32_t)ry,     grip_col);
        }
    }

    /* Render Mouse Cursor */
    if (cursor_x >= 0 && (uint32_t)cursor_x < surf->width && cursor_y >= 0 && (uint32_t)cursor_y < surf->height) {
        uint32_t cx = (uint32_t)cursor_x;
        uint32_t cy = (uint32_t)cursor_y;
        /* Arrow cursor outline and fill */
        for (int32_t row = 0; row < 12; row++) {
            for (int32_t col = 0; col <= row; col++) {
                if (cx + (uint32_t)col < surf->width && cy + (uint32_t)row < surf->height) {
                    uint32_t color = (col == 0 || col == row || row == 11) ? GFX_COLOR_WHITE : GFX_COLOR_EMERALD_ACCENT;
                    gfx_put_pixel(surf, cx + (uint32_t)col, cy + (uint32_t)row, color);
                }
            }
        }
    }
}

/* Self-test */
bool wm_self_test(void) {
    if (!wm_initialize()) return false;
    size_t prev_count = wm_get_window_count();

    struct window *w1 = wm_create_window("Terminal", 50, 50, 400, 300, WM_FLAG_NONE);
    if (w1 == NULL) return false;
    if (!str_equal(w1->title, "Terminal") || w1->width != 400 || w1->height != 300) return false;

    struct window *w2 = wm_create_window("Files", 100, 100, 500, 350, WM_FLAG_NONE);
    if (w2 == NULL) return false;

    /* Top window should be w2 */
    if (wm_get_top_window() != w2) return false;
    if (!w2->focused || w1->focused) return false;

    /* Focus w1 */
    if (wm_focus_window(w1->id) != 0) return false;
    if (wm_get_top_window() != w1) return false;
    if (!w1->focused || w2->focused) return false;

    /* Move window */
    if (wm_move_window(w1->id, 75, 80) != 0) return false;
    if (w1->x != 75 || w1->y != 80) return false;

    /* Resize window */
    if (wm_resize_window(w1->id, 450, 320) != 0) return false;
    if (w1->width != 450 || w1->height != 320) return false;
    /* Clamp resize to minimum */
    if (wm_resize_window(w1->id, 50, 40) != 0) return false;
    if (w1->width != w1->min_width || w1->height != w1->min_height) return false;
    if (wm_resize_window(w1->id, 400, 300) != 0) return false;

    /* Maximize window (bounded by top status bar and bottom taskbar) */
    if (wm_maximize_window(w1->id) != 0) return false;
    if (!w1->maximized || w1->x != 0 || w1->y != (int32_t)WM_TOP_BAR_HEIGHT) return false;
    /* Unmaximize */
    if (wm_maximize_window(w1->id) != 0) return false;
    if (w1->maximized || w1->x != 75 || w1->y != 80) return false;

    /* Minimize window */
    if (wm_minimize_window(w1->id) != 0) return false;
    if (!w1->minimized) return false;

    /* Un-minimize w1 by focusing it */
    if (wm_focus_window(w1->id) != 0) return false;
    if (w1->minimized) return false;

    /* Workspace isolation and switching test */
    if (wm_get_current_workspace() != 0U) return false;
    if (wm_set_current_workspace(1U) != 0) return false;
    if (wm_get_current_workspace() != 1U) return false;

    /* Windows w1 and w2 are on workspace 0; on workspace 1 they must not be top or found */
    if (wm_get_top_window() != NULL) return false;
    if (wm_find_window_at(w1->x, w1->y) != NULL) return false;

    /* Create window on workspace 1 */
    struct window *w3 = wm_create_window("WS1-App", 120, 120, 300, 200, WM_FLAG_NONE);
    if (w3 == NULL) return false;
    if (w3->workspace != 1U) return false;
    if (wm_get_top_window() != w3) return false;
    if (wm_find_window_at(125, 125) != w3) return false;

    /* Move w1 to workspace 1 */
    if (wm_move_window_to_workspace(w1->id, 1U) != 0) return false;
    if (w1->workspace != 1U) return false;
    if (wm_find_window_at(w1->x, w1->y) != w1) return false;

    /* Move w1 back to workspace 0 */
    if (wm_move_window_to_workspace(w1->id, 0U) != 0) return false;
    /* Destroy w3 */
    if (wm_destroy_window(w3->id) != 0) return false;

    /* Switch back to workspace 0 */
    if (wm_set_current_workspace(0U) != 0) return false;
    if (wm_get_top_window() != w1) return false;

    /* Mouse bottom-right resize grip interaction test */
    {
        int32_t grip_x = w1->x + (int32_t)w1->width - 4;
        int32_t grip_y = w1->y + (int32_t)w1->height - 4;
        struct mouse_event me_press = { .x = grip_x, .y = grip_y, .left_button = true, .right_button = false, .middle_button = false, .dx = 0, .dy = 0 };
        wm_handle_mouse(&me_press);
        if (!w1->resizing) return false;

        struct mouse_event me_drag = { .x = grip_x + 30, .y = grip_y + 20, .left_button = true, .right_button = false, .middle_button = false, .dx = 30, .dy = 20 };
        wm_handle_mouse(&me_drag);
        if (w1->width != 400U + 30U || w1->height != 300U + 20U) return false;

        struct mouse_event me_release = { .x = grip_x + 30, .y = grip_y + 20, .left_button = false, .right_button = false, .middle_button = false, .dx = 0, .dy = 0 };
        wm_handle_mouse(&me_release);
        if (w1->resizing) return false;
        (void)wm_resize_window(w1->id, 400U, 300U);
    }

    /* Focus cycling (Alt+Tab equivalent) */
    /* w1 is top, w2 is background */
    if (wm_get_top_window() != w1) return false;
    if (wm_cycle_focus(true) != 0) return false;
    if (wm_get_top_window() != w2 || !w2->focused || w1->focused) return false;

    /* wm_handle_key with Alt+Tab */
    struct key_event alt_tab = {
        .scancode = 0x0FU,
        .keycode = KEY_TAB,
        .ascii = '\t',
        .pressed = true,
        .shift = false,
        .ctrl = false,
        .alt = true,
        .gui = false
    };
    wm_handle_key(&alt_tab);
    if (wm_get_top_window() != w1 || !w1->focused || w2->focused) return false;

    /* wm_handle_key with Alt+F4 closes active window */
    struct key_event alt_f4 = {
        .scancode = 0x3EU,
        .keycode = KEY_F4,
        .pressed = true,
        .shift = false,
        .ctrl = false,
        .alt = true,
        .gui = false
    };
    wm_handle_key(&alt_f4);
    if (wm_get_top_window() != w2) return false;
    if (wm_get_window_count() != prev_count + 1U) return false;

    /* Destroy remaining window w2 */
    if (wm_destroy_window(w2->id) != 0) return false;
    if (wm_get_window_count() != prev_count) return false;

    return true;
}
