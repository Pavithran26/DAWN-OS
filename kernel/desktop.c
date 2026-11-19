/* SPDX-License-Identifier: Unlicense */
#include "desktop.h"
#include "os_metadata.h"

static struct desktop_state state = {0};
static bool desktop_initialized = false;

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

static size_t str_length(const char *text) {
    size_t length = 0U;
    while (text != NULL && text[length] != '\0') ++length;
    return length;
}

static uint32_t footer_x(uint32_t screen_width) {
    return (screen_width > 500U) ? (screen_width - 460U) : 56U;
}

bool desktop_open_url(const char *url) {
    char checked[DAWN_META_URL_MAX];
    const struct dawn_metadata_view *meta = dawn_metadata_get();
    if (meta == NULL) return false;
    if (!dawn_url_copy(url, checked, sizeof(checked))) return false;
    /* No system browser exists in this build yet; report the validated target
     * instead of claiming that a page was opened. */
    desktop_show_notification("No browser available", checked);
    return true;
}

bool desktop_initialize(void) {
    if (desktop_initialized) return true;

    gfx_initialize();
    wm_initialize();

    struct gfx_surface *fb = gfx_get_framebuffer();
    state.screen_width = (fb && fb->width > 0) ? fb->width : 1024U;
    state.screen_height = (fb && fb->height > 0) ? fb->height : 768U;
    state.mouse_x = (int32_t)(state.screen_width / 2U);
    state.mouse_y = (int32_t)(state.screen_height / 2U);
    state.launcher_open = false;

    for (size_t i = 0; i < DESKTOP_MAX_NOTIFICATIONS; i++) {
        state.notifications[i] = (struct notification){0};
    }

    desktop_initialized = true;
    return true;
}

void desktop_toggle_launcher(void) {
    state.launcher_open = !state.launcher_open;
}

void desktop_show_notification(const char *title, const char *msg) {
    for (size_t i = 0; i < DESKTOP_MAX_NOTIFICATIONS; i++) {
        if (!state.notifications[i].active) {
            str_copy(state.notifications[i].title, title ? title : "Notification", 32);
            str_copy(state.notifications[i].message, msg ? msg : "", 64);
            state.notifications[i].ticks_remaining = 300;
            state.notifications[i].active = true;
            return;
        }
    }
}

void desktop_handle_input_event(const struct input_event *event) {
    if (event == NULL) return;

    if (event->type == INPUT_EVENT_MOUSE) {
        const struct mouse_event *mevt = &event->mouse;
        state.mouse_x = mevt->x;
        state.mouse_y = mevt->y;

        uint32_t tb_y = state.screen_height - DESKTOP_TASKBAR_HEIGHT;

        if (mevt->left_button) {
            /* Check Start Button: x in [8, 44], y in [tb_y + 8, tb_y + 40] */
            if (mevt->x >= 8 && mevt->x <= 44 && mevt->y >= (int32_t)(tb_y + 8)) {
                desktop_toggle_launcher();
                return;
            }

            /* Check Launcher items if launcher open */
            if (state.launcher_open) {
                int32_t lx = 8;
                int32_t ly = (int32_t)(tb_y - 240);
                if (mevt->x >= lx && mevt->x <= lx + 220 && mevt->y >= ly && mevt->y <= (int32_t)tb_y) {
                    /* Item 1: Terminal (ly + 40 to ly + 70) */
                    if (mevt->y >= ly + 40 && mevt->y < ly + 70) {
                        wm_create_window("Terminal", 100, 100, 560, 380, WM_FLAG_NONE);
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 2: File Explorer (ly + 70 to ly + 100) */
                    if (mevt->y >= ly + 70 && mevt->y < ly + 100) {
                        wm_create_window("File Explorer", 150, 120, 600, 420, WM_FLAG_NONE);
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 3: Task Manager (ly + 100 to ly + 130) */
                    if (mevt->y >= ly + 100 && mevt->y < ly + 130) {
                        wm_create_window("Task Manager", 200, 140, 520, 360, WM_FLAG_NONE);
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 4: Settings (ly + 130 to ly + 160) */
                    if (mevt->y >= ly + 130 && mevt->y < ly + 160) {
                        wm_create_window("Settings", 220, 160, 480, 340, WM_FLAG_NONE);
                        state.launcher_open = false;
                        return;
                    }
                } else {
                    /* Clicked outside launcher */
                    state.launcher_open = false;
                }
            }

            /* Clickable developer domain in the taskbar footer */
            {
                const struct dawn_metadata_view *meta = dawn_metadata_get();
                if (meta != NULL && mevt->y >= (int32_t)(tb_y + 30U) &&
                    mevt->y <= (int32_t)(tb_y + 46U)) {
                    size_t total = str_length(meta->footer_text);
                    size_t domain = str_length(meta->footer_domain);
                    if (total > domain) {
                        uint32_t domain_x = footer_x(state.screen_width) +
                                            (uint32_t)((total - domain) * 8U);
                        if (mevt->x >= (int32_t)domain_x &&
                            mevt->x <= (int32_t)(domain_x + (domain * 8U))) {
                            (void)desktop_open_url(meta->developer_website);
                            return;
                        }
                    }
                }
            }

            /* Check Taskbar Window Tabs */
            if (mevt->y >= (int32_t)tb_y) {
                uint32_t tab_x = 60;
                size_t wcount = wm_get_window_count();
                for (size_t i = 0; i < wcount; i++) {
                    if (mevt->x >= (int32_t)tab_x && mevt->x <= (int32_t)(tab_x + 120)) {
                        struct window *win = wm_get_window_at_z(i);
                        if (win != NULL) {
                            if (win->focused) {
                                wm_minimize_window(win->id);
                            } else {
                                wm_focus_window(win->id);
                            }
                        }
                        return;
                    }
                    tab_x += 128;
                }
                return;
            }
        }

        /* Forward to window manager */
        wm_handle_mouse(mevt);
    }
}

void desktop_render(struct gfx_surface *surf) {
    if (surf == NULL) return;

    /* Render windows, background, and cursor via window manager */
    wm_composite(surf, state.mouse_x, state.mouse_y);

    /* Render Desktop Icons on left */
    gfx_draw_icon(surf, 24, 24, GFX_ICON_COMPUTER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 16, 44, "This PC", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 94, GFX_ICON_FILE_EXPLORER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 20, 114, "Files", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 164, GFX_ICON_TERMINAL, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 14, 184, "Terminal", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 234, GFX_ICON_TASK_MANAGER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 12, 254, "Task Mgr", GFX_COLOR_TEXT_PRIMARY);

    /* Render Taskbar at bottom */
    uint32_t tb_y = surf->height - DESKTOP_TASKBAR_HEIGHT;
    gfx_draw_glass_panel(surf, 0, tb_y, surf->width, DESKTOP_TASKBAR_HEIGHT, GFX_COLOR_GLASS_PANEL);

    /* Start Button with Emerald Logo */
    gfx_draw_glass_panel(surf, 8, tb_y + 8, 36, 32, GFX_COLOR_EMERALD_PRIMARY);
    gfx_draw_icon(surf, 18, tb_y + 16, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_HIGHLIGHT);

    /* Window Switcher Tabs */
    uint32_t tab_x = 56;
    size_t wcount = wm_get_window_count();
    for (size_t i = 0; i < wcount; i++) {
        struct window *win = wm_get_window_at_z(i);
        if (win != NULL) {
            uint32_t tab_color = win->focused ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_BASE;
            gfx_draw_glass_panel(surf, tab_x, tb_y + 8, 120, 32, tab_color);
            gfx_draw_string(surf, tab_x + 8, tb_y + 16, win->title, GFX_COLOR_TEXT_PRIMARY);
            if (win->focused) {
                /* Glowing bottom underline */
                gfx_draw_line(surf, (int32_t)tab_x + 4, (int32_t)(tb_y + 36), (int32_t)(tab_x + 116), (int32_t)(tb_y + 36), GFX_COLOR_EMERALD_ACCENT);
            }
        }
        tab_x += 128;
    }

    /* System Tray */
    uint32_t tray_x = surf->width - 180;
    gfx_draw_string(surf, tray_x, tb_y + 16, "[NET] [VOL] 12:00", GFX_COLOR_TEXT_SECONDARY);

    /* Developer footer from the central metadata source. The domain is
     * underlined and click-handled above. */
    {
        const struct dawn_metadata_view *meta = dawn_metadata_get();
        if (meta != NULL) {
            uint32_t fx = footer_x(surf->width);
            uint32_t fy = tb_y + 30U;
            size_t total = str_length(meta->footer_text);
            size_t domain = str_length(meta->footer_domain);
            gfx_draw_string(surf, fx, fy, meta->footer_text, GFX_COLOR_TEXT_MUTED);
            if (total > domain) {
                uint32_t domain_x = fx + (uint32_t)((total - domain) * 8U);
                gfx_draw_line(surf, (int32_t)domain_x, (int32_t)(fy + 16U),
                              (int32_t)(domain_x + (uint32_t)(domain * 8U)),
                              (int32_t)(fy + 16U), GFX_COLOR_EMERALD_ACCENT);
            }
        }
    }

    /* Render Start Menu / App Launcher if open */
    if (state.launcher_open) {
        int32_t lx = 8;
        int32_t ly = (int32_t)(tb_y - 240);
        gfx_draw_glass_panel(surf, (uint32_t)lx, (uint32_t)ly, 220, 236, GFX_COLOR_GLASS_PANEL);

        /* Header */
        gfx_draw_icon(surf, (uint32_t)(lx + 12), (uint32_t)(ly + 12), GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 36), (uint32_t)(ly + 12), "DAWN-OS", GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_line(surf, lx + 8, ly + 34, lx + 212, ly + 34, GFX_COLOR_GLASS_BORDER);

        /* Menu Items */
        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 46), GFX_ICON_TERMINAL, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 46), "Terminal", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 76), GFX_ICON_FILE_EXPLORER, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 76), "File Explorer", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 106), GFX_ICON_TASK_MANAGER, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 106), "Task Manager", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 136), GFX_ICON_SETTINGS, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 136), "Settings", GFX_COLOR_TEXT_PRIMARY);

        /* Shutdown button at bottom */
        gfx_draw_line(surf, lx + 8, ly + 180, lx + 212, ly + 180, GFX_COLOR_GLASS_BORDER);
        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 194), GFX_ICON_SHUTDOWN, GFX_COLOR_BTN_CLOSE);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 194), "Shut Down", GFX_COLOR_TEXT_PRIMARY);
    }

    /* Render Notifications in top right */
    uint32_t notif_y = 16;
    for (size_t i = 0; i < DESKTOP_MAX_NOTIFICATIONS; i++) {
        if (state.notifications[i].active) {
            uint32_t notif_x = surf->width - 260;
            gfx_draw_glass_panel(surf, notif_x, notif_y, 244, 52, GFX_COLOR_GLASS_PANEL);
            gfx_draw_icon(surf, notif_x + 8, notif_y + 10, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
            gfx_draw_string(surf, notif_x + 30, notif_y + 8, state.notifications[i].title, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(surf, notif_x + 30, notif_y + 26, state.notifications[i].message, GFX_COLOR_TEXT_MUTED);

            notif_y += 60;
        }
    }
}

/* Self-test */
static uint32_t test_render_buf[640 * 480];

bool desktop_self_test(void) {
    if (!desktop_initialize()) return false;

    /* Notification test */
    desktop_show_notification("DAWN-OS", "System Ready");
    if (!state.notifications[0].active) return false;
    if (!str_equal(state.notifications[0].title, "DAWN-OS")) return false;

    /* Launcher toggle */
    if (state.launcher_open) return false;
    desktop_toggle_launcher();
    if (!state.launcher_open) return false;
    desktop_toggle_launcher();
    if (state.launcher_open) return false;

    /* Render test into a 640x480 test surface */
    struct gfx_surface test_surf = {
        .buffer = test_render_buf,
        .width = 640,
        .height = 480,
        .pitch = 640 * sizeof(uint32_t)
    };

    desktop_render(&test_surf);

    /* Verify taskbar was drawn (check bottom pixels not all zero) */
    uint32_t tb_pixel = test_surf.buffer[(480 - 10) * 640 + 20];
    if (tb_pixel == 0) return false;

    return true;
}
