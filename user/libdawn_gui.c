/* SPDX-License-Identifier: Unlicense */
#include "dawn_gui.h"

static struct dawn_gui_window client_win_pool[4];
static bool client_win_used[4] = {false};

dawn_window_t dawn_gui_create_window(const char *title, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    (void)title;
    for (int i = 0; i < 4; i++) {
        if (!client_win_used[i]) {
            client_win_used[i] = true;
            client_win_pool[i].id = (uint64_t)(i + 1);
            client_win_pool[i].x = x;
            client_win_pool[i].y = y;
            client_win_pool[i].width = width;
            client_win_pool[i].height = height;
            client_win_pool[i].buffer = (uint32_t *)dawn_malloc(width * height * sizeof(uint32_t));
            client_win_pool[i].active = true;

            if (client_win_pool[i].buffer != NULL) {
                dawn_memset(client_win_pool[i].buffer, 0, width * height * sizeof(uint32_t));
            }
            return &client_win_pool[i];
        }
    }
    return NULL;
}

void dawn_gui_destroy_window(dawn_window_t win) {
    if (win == NULL) return;
    if (win->buffer != NULL) {
        dawn_free(win->buffer);
        win->buffer = NULL;
    }
    win->active = false;
    for (int i = 0; i < 4; i++) {
        if (&client_win_pool[i] == win) {
            client_win_used[i] = false;
            break;
        }
    }
}

void dawn_gui_clear(dawn_window_t win, uint32_t color) {
    if (win == NULL || win->buffer == NULL) return;
    size_t count = (size_t)win->width * (size_t)win->height;
    for (size_t i = 0; i < count; i++) {
        win->buffer[i] = color;
    }
}

void dawn_gui_draw_rect(dawn_window_t win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (win == NULL || win->buffer == NULL) return;
    for (uint32_t row = 0; row < h; row++) {
        int32_t cy = y + (int32_t)row;
        if (cy < 0 || (uint32_t)cy >= win->height) continue;
        for (uint32_t col = 0; col < w; col++) {
            int32_t cx = x + (int32_t)col;
            if (cx < 0 || (uint32_t)cx >= win->width) continue;
            win->buffer[(uint32_t)cy * win->width + (uint32_t)cx] = color;
        }
    }
}

void dawn_gui_draw_text(dawn_window_t win, int32_t x, int32_t y, const char *text, uint32_t color) {
    if (win == NULL || win->buffer == NULL || text == NULL) return;
    int32_t cur_x = x;
    while (*text != '\0') {
        /* Draw 8x8 glyph placeholder / bounding block */
        dawn_gui_draw_rect(win, cur_x, y, 7, 10, color);
        cur_x += 8;
        text++;
    }
}

void dawn_gui_commit(dawn_window_t win) {
    if (win == NULL || win->buffer == NULL) return;
    /* In display server architecture, buffer is committed to compositor */
}

bool dawn_gui_poll_event(dawn_window_t win, struct dawn_gui_event *out_event) {
    if (win == NULL || out_event == NULL) return false;
    out_event->type = DAWN_GUI_EVENT_NONE;
    return false;
}
