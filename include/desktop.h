/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_DESKTOP_H
#define DAWN_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "window_manager.h"
#include "input.h"

#define DESKTOP_STATUSBAR_HEIGHT 28U
#define DESKTOP_TASKBAR_HEIGHT   48U
#define DESKTOP_MAX_NOTIFICATIONS 4U

struct notification {
    char title[32];
    char message[64];
    uint32_t ticks_remaining;
    bool active;
};

struct desktop_state {
    bool launcher_open;
    int32_t mouse_x;
    int32_t mouse_y;
    struct notification notifications[DESKTOP_MAX_NOTIFICATIONS];
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t current_workspace;
};

bool desktop_initialize(void);
void desktop_handle_input_event(const struct input_event *event);
void desktop_render(struct gfx_surface *surf);
void desktop_show_notification(const char *title, const char *msg);
/* Validates a URL and opens it with the system browser when one is available.
 * When no browser exists, shows a notification containing the validated URL. */
bool desktop_open_url(const char *url);
void desktop_toggle_launcher(void);
void desktop_switch_workspace(uint32_t ws);
uint32_t desktop_get_workspace(void);

bool desktop_self_test(void);

#endif /* DAWN_DESKTOP_H */
