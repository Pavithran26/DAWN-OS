/* SPDX-License-Identifier: Unlicense */
#include "desktop.h"
#include "os_metadata.h"
#include "settings.h"
#include "power.h"
#include "rtc.h"
#include "calculator.h"
#include "calendar.h"
#include "terminal.h"

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
    const struct dawn_metadata_view *meta = dawn_metadata_get();
    if (meta == NULL) return 56U;
    uint32_t total_w = (uint32_t)(str_length(meta->footer_text) * 8U);
    if (screen_width > total_w + 200U) {
        return (screen_width / 2U) - (total_w / 2U);
    }
    return 56U;
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

void desktop_switch_workspace(uint32_t ws) {
    if (ws >= WM_MAX_WORKSPACES) return;
    state.current_workspace = ws;
    (void)wm_set_current_workspace(ws);
}

uint32_t desktop_get_workspace(void) {
    return wm_get_current_workspace();
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

    if (event->type == INPUT_EVENT_KEYBOARD) {
        const struct key_event *kevt = &event->key;
        if (kevt->pressed) {
            /* Super key (Windows/GUI key) or Ctrl+Escape: toggle application launcher */
            if (kevt->keycode == KEY_SUPER || (kevt->ctrl && kevt->keycode == KEY_ESCAPE)) {
                desktop_toggle_launcher();
                return;
            }
            /* Escape key: dismiss application launcher if currently open */
            if (kevt->keycode == KEY_ESCAPE && state.launcher_open) {
                state.launcher_open = false;
                return;
            }

            /* Super + 1..4: direct workspace jump */
            if (kevt->gui && kevt->keycode >= KEY_1 && kevt->keycode <= KEY_4) {
                uint32_t target_ws = (uint32_t)(kevt->keycode - KEY_1);
                desktop_switch_workspace(target_ws);
                return;
            }

            /* Ctrl + Alt + Left/Right: workspace switching */
            if (kevt->ctrl && kevt->alt && kevt->keycode == KEY_LEFT) {
                uint32_t cur = wm_get_current_workspace();
                uint32_t prev_ws = (cur + WM_MAX_WORKSPACES - 1U) % WM_MAX_WORKSPACES;
                desktop_switch_workspace(prev_ws);
                return;
            }
            if (kevt->ctrl && kevt->alt && kevt->keycode == KEY_RIGHT) {
                uint32_t cur = wm_get_current_workspace();
                uint32_t next_ws = (cur + 1U) % WM_MAX_WORKSPACES;
                desktop_switch_workspace(next_ws);
                return;
            }
        }
        /* Forward all keyboard events to window manager for focus cycling, shortcuts, and active window dispatch */
        wm_handle_key(kevt);
        return;
    }

    if (event->type == INPUT_EVENT_MOUSE) {
        const struct mouse_event *mevt = &event->mouse;
        state.mouse_x = mevt->x;
        state.mouse_y = mevt->y;

        uint32_t tb_y = state.screen_height - DESKTOP_TASKBAR_HEIGHT;
        static bool prev_left_button = false;
        bool is_press_edge = mevt->left_button && !prev_left_button;
        prev_left_button = mevt->left_button;

        if (is_press_edge) {
            /* Check Top Status Bar Clicks */
            if (mevt->y >= 0 && mevt->y < (int32_t)DESKTOP_STATUSBAR_HEIGHT) {
                /* Workspace Switcher pills at x in [96 .. 96 + WM_MAX_WORKSPACES * 28] */
                if (mevt->x >= 96 && mevt->x < (int32_t)(96U + WM_MAX_WORKSPACES * 28U)) {
                    uint32_t clicked_ws = (uint32_t)(mevt->x - 96) / 28U;
                    if (clicked_ws < WM_MAX_WORKSPACES) {
                        desktop_switch_workspace(clicked_ws);
                        return;
                    }
                }
                return;
            }
            /* Check Start Button: x in [8, 44], y in [tb_y + 8, tb_y + 40] */
            if (mevt->x >= 8 && mevt->x <= 44 && mevt->y >= (int32_t)(tb_y + 8)) {
                desktop_toggle_launcher();
                return;
            }

            /* Check Launcher items if launcher open */
            if (state.launcher_open) {
                int32_t lx = 8;
                int32_t ly = (int32_t)(tb_y - 350);
                if (mevt->x >= lx && mevt->x <= lx + 220 && mevt->y >= ly && mevt->y <= (int32_t)tb_y) {
                    /* Item 1: Terminal (ly + 38 to ly + 64) */
                    if (mevt->y >= ly + 38 && mevt->y < ly + 64) {
                        (void)terminal_window_open();
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 2: File Explorer (ly + 66 to ly + 92) */
                    if (mevt->y >= ly + 66 && mevt->y < ly + 92) {
                        struct window *existing = wm_find_window_by_title("File Explorer");
                        if (existing != NULL) {
                            if (existing->minimized) wm_minimize_window(existing->id);
                            wm_focus_window(existing->id);
                        } else {
                            wm_create_window("File Explorer", 150, 120, 600, 420, WM_FLAG_NONE);
                        }
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 3: Task Manager (ly + 94 to ly + 120) */
                    if (mevt->y >= ly + 94 && mevt->y < ly + 120) {
                        struct window *existing = wm_find_window_by_title("Task Manager");
                        if (existing != NULL) {
                            if (existing->minimized) wm_minimize_window(existing->id);
                            wm_focus_window(existing->id);
                        } else {
                            wm_create_window("Task Manager", 200, 140, 520, 360, WM_FLAG_NONE);
                        }
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 4: Calculator (ly + 122 to ly + 148) */
                    if (mevt->y >= ly + 122 && mevt->y < ly + 148) {
                        calculator_window_open();
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 5: Calendar (ly + 150 to ly + 176) */
                    if (mevt->y >= ly + 150 && mevt->y < ly + 176) {
                        calendar_window_open();
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 6: Settings (ly + 178 to ly + 204) */
                    if (mevt->y >= ly + 178 && mevt->y < ly + 204) {
                        settings_window_open();
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 7: Help & Support (ly + 206 to ly + 232) */
                    if (mevt->y >= ly + 206 && mevt->y < ly + 232) {
                        settings_window_open_tab(TAB_HELP_SUPPORT);
                        state.launcher_open = false;
                        return;
                    }
                    /* Item 8: About DAWN-OS (ly + 234 to ly + 264) */
                    if (mevt->y >= ly + 234 && mevt->y < ly + 264) {
                        settings_window_open_tab(TAB_ABOUT);
                        state.launcher_open = false;
                        return;
                    }
                    /* Shutdown button (ly + 280 to ly + 340) */
                    if (mevt->y >= ly + 280 && mevt->y < ly + 340) {
                        desktop_show_notification("Power", "System Shut Down initiated");
                        state.launcher_open = false;
                        power_shutdown();
                        return;
                    }
                } else {
                    /* Clicked outside launcher */
                    state.launcher_open = false;
                }
            }

            /* Check Tray Clicks: [NET] (0..48), [VOL] (48..96), Clock (96..180) */
            if (mevt->y >= (int32_t)tb_y && mevt->x >= (int32_t)(state.screen_width - 180)) {
                int32_t tray_offset = mevt->x - (int32_t)(state.screen_width - 180);
                if (tray_offset < 48) {
                    settings_window_open_tab(TAB_NETWORK);
                } else if (tray_offset < 96) {
                    settings_window_open_tab(TAB_AUDIO);
                } else {
                    calendar_window_open();
                }
                return;
            }

            /* Check Taskbar Window Tabs BEFORE Taskbar footer */
            if (mevt->y >= (int32_t)tb_y) {
                uint32_t tab_x = 60;
                size_t wcount = wm_get_window_count();
                uint32_t cur_ws = wm_get_current_workspace();
                for (size_t i = 0; i < wcount; i++) {
                    struct window *win = wm_get_window_at_z(i);
                    if (win != NULL && (win->workspace == cur_ws || (win->flags & WM_FLAG_ALL_WORKSPACES))) {
                        if (mevt->x >= (int32_t)tab_x && mevt->x <= (int32_t)(tab_x + 120)) {
                            if (win->focused) {
                                wm_minimize_window(win->id);
                            } else {
                                wm_focus_window(win->id);
                            }
                            return;
                        }
                        tab_x += 128;
                    }
                }
            }

            /* Clickable developer domain in the taskbar footer */
            {
                const struct dawn_metadata_view *meta = dawn_metadata_get();
                if (meta != NULL && mevt->y >= (int32_t)(tb_y + 16U) &&
                    mevt->y <= (int32_t)(tb_y + 32U)) {
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

            /* Check Desktop Icons when click is not captured by a window */
            if (wm_find_window_at(mevt->x, mevt->y) == NULL &&
                mevt->y >= (int32_t)DESKTOP_STATUSBAR_HEIGHT && mevt->y < (int32_t)tb_y) {
                if (mevt->x >= 8 && mevt->x <= 84) {
                    /* This PC -> System Info */
                    if (mevt->y >= 28 && mevt->y < 72) {
                        settings_window_open_tab(TAB_SYSTEM_INFO);
                        return;
                    }
                    /* Files -> File Explorer */
                    if (mevt->y >= 72 && mevt->y < 124) {
                        struct window *existing = wm_find_window_by_title("File Explorer");
                        if (existing != NULL) {
                            if (existing->minimized) wm_minimize_window(existing->id);
                            wm_focus_window(existing->id);
                        } else {
                            wm_create_window("File Explorer", 150, 120, 600, 420, WM_FLAG_NONE);
                        }
                        return;
                    }
                    /* Terminal */
                    if (mevt->y >= 128 && mevt->y < 180) {
                        (void)terminal_window_open();
                        return;
                    }
                    /* Task Mgr */
                    if (mevt->y >= 184 && mevt->y < 236) {
                        struct window *existing = wm_find_window_by_title("Task Manager");
                        if (existing != NULL) {
                            if (existing->minimized) wm_minimize_window(existing->id);
                            wm_focus_window(existing->id);
                        } else {
                            wm_create_window("Task Manager", 200, 140, 520, 360, WM_FLAG_NONE);
                        }
                        return;
                    }
                    /* Settings */
                    if (mevt->y >= 352 && mevt->y < 404) {
                        settings_window_open();
                        return;
                    }
                    /* Calculator */
                    if (mevt->y >= 240 && mevt->y < 292) {
                        calculator_window_open();
                        return;
                    }
                    /* Calendar */
                    if (mevt->y >= 296 && mevt->y < 348) {
                        calendar_window_open();
                        return;
                    }
                    /* About OS */
                    if (mevt->y >= 408 && mevt->y < 460) {
                        settings_window_open_tab(TAB_ABOUT);
                        return;
                    }
                    /* Help */
                    if (mevt->y >= 464 && mevt->y < 516) {
                        settings_window_open_tab(TAB_HELP_SUPPORT);
                        return;
                    }
                }
            }
        }

        /* Forward to window manager */
        wm_handle_mouse(mevt);
    }
}

void desktop_render(struct gfx_surface *surf) {
    if (surf == NULL) return;

    /* Keep desktop input handling and boundaries synchronized with surface resolution */
    state.screen_width = surf->width;
    state.screen_height = surf->height;

    /* Render windows, background, and cursor via window manager */
    wm_composite(surf, state.mouse_x, state.mouse_y);

    /* Render Top Status Bar (y = 0 to DESKTOP_STATUSBAR_HEIGHT) */
    gfx_draw_rect_filled(surf, 0, 0, surf->width, DESKTOP_STATUSBAR_HEIGHT, GFX_COLOR_EMERALD_BASE);
    gfx_draw_glass_panel(surf, 0, 0, surf->width, DESKTOP_STATUSBAR_HEIGHT, GFX_COLOR_GLASS_SURFACE);
    gfx_draw_line(surf, 0, (int32_t)(DESKTOP_STATUSBAR_HEIGHT - 1U), (int32_t)(surf->width - 1U), (int32_t)(DESKTOP_STATUSBAR_HEIGHT - 1U), GFX_COLOR_EMERALD_ACCENT);

    /* Left: Brand badge */
    gfx_draw_icon(surf, 8, 6, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_HIGHLIGHT);
    gfx_draw_string(surf, 26, 6, "DAWN-OS", GFX_COLOR_TEXT_PRIMARY);

    /* Workspace Switcher pills: [1] [2] [3] [4] */
    uint32_t cur_ws = wm_get_current_workspace();
    for (uint32_t w = 0; w < WM_MAX_WORKSPACES; w++) {
        uint32_t pill_x = 96U + w * 28U;
        bool is_active = (w == cur_ws);
        uint32_t pill_bg = is_active ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_SURFACE;
        gfx_draw_glass_panel(surf, pill_x, 4, 24, 20, pill_bg);
        if (is_active) {
            gfx_draw_rect_outline(surf, pill_x, 4, 24, 20, GFX_COLOR_EMERALD_ACCENT);
        }
        char ws_char = (char)('1' + w);
        char ws_str[2] = { ws_char, '\0' };
        gfx_draw_string(surf, pill_x + 8, 6, ws_str, is_active ? GFX_COLOR_EMERALD_HIGHLIGHT : GFX_COLOR_TEXT_MUTED);
    }

    /* Center: System Telemetry */
    if (surf->width > 480U) {
        uint32_t mid_x = surf->width / 2U;
        gfx_draw_string(surf, mid_x - 90U, 6, "CPU: 0.2% | RAM: 24M/128M", GFX_COLOR_TEXT_SECONDARY);
    }

    /* Right: Session info */
    if (surf->width > 320U) {
        gfx_draw_string(surf, surf->width - 100U, 6, "admin@dawn", GFX_COLOR_EMERALD_CYAN);
    }

    /* Render Desktop Icons on left (offset below top status bar) */
    gfx_draw_icon(surf, 24, 36, GFX_ICON_COMPUTER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 16, 56, "This PC", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 76, GFX_ICON_FILE_EXPLORER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 20, 96, "Files", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 132, GFX_ICON_TERMINAL, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 14, 152, "Terminal", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 188, GFX_ICON_TASK_MANAGER, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 12, 208, "Task Mgr", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 244, GFX_ICON_CALCULATOR, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 22, 264, "Calc", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 300, GFX_ICON_CALENDAR, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 10, 320, "Calendar", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 356, GFX_ICON_SETTINGS, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 14, 376, "Settings", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 412, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 14, 432, "About OS", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_icon(surf, 24, 468, GFX_ICON_SETTINGS, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(surf, 16, 488, "Help", GFX_COLOR_TEXT_PRIMARY);

    /* Render Taskbar at bottom */
    uint32_t tb_y = (surf->height > DESKTOP_TASKBAR_HEIGHT) ? (surf->height - DESKTOP_TASKBAR_HEIGHT) : 0U;

    /* High-contrast solid dark emerald base panel */
    gfx_draw_rect_filled(surf, 0, tb_y, surf->width, DESKTOP_TASKBAR_HEIGHT, GFX_COLOR_EMERALD_BASE);
    /* Frosted glass overlay */
    gfx_draw_glass_panel(surf, 0, tb_y, surf->width, DESKTOP_TASKBAR_HEIGHT, GFX_COLOR_GLASS_SURFACE);

    /* Glowing specular neon emerald accent line along the top edge of taskbar */
    gfx_draw_line(surf, 0, (int32_t)tb_y, (int32_t)(surf->width - 1), (int32_t)tb_y, GFX_COLOR_EMERALD_ACCENT);

    /* Start Button with Emerald Logo and border */
    gfx_draw_glass_panel(surf, 8, tb_y + 8, 36, 32, GFX_COLOR_EMERALD_PRIMARY);
    gfx_draw_rect_outline(surf, 8, tb_y + 8, 36, 32, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_icon(surf, 18, tb_y + 16, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_HIGHLIGHT);

    /* Window Switcher Tabs (current workspace) */
    uint32_t tab_x = 56;
    size_t wcount = wm_get_window_count();
    for (size_t i = 0; i < wcount; i++) {
        struct window *win = wm_get_window_at_z(i);
        if (win != NULL && (win->workspace == cur_ws || (win->flags & WM_FLAG_ALL_WORKSPACES))) {
            uint32_t tab_color = win->focused ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_BASE;
            gfx_draw_glass_panel(surf, tab_x, tb_y + 8, 120, 32, tab_color);
            gfx_draw_rect_outline(surf, tab_x, tb_y + 8, 120, 32, win->focused ? GFX_COLOR_EMERALD_ACCENT : GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(surf, tab_x + 8, tb_y + 16, win->title, GFX_COLOR_TEXT_PRIMARY);
            if (win->focused) {
                /* Glowing bottom underline */
                gfx_draw_line(surf, (int32_t)tab_x + 4, (int32_t)(tb_y + 36), (int32_t)(tab_x + 116), (int32_t)(tb_y + 36), GFX_COLOR_EMERALD_ACCENT);
            }
            tab_x += 128;
        }
    }

    /* System Tray with dynamic RTC clock in a dedicated glass pill container */
    uint32_t tray_w = 176U;
    uint32_t tray_x = (surf->width > tray_w + 8U) ? (surf->width - tray_w - 8U) : 0U;
    gfx_draw_glass_panel(surf, tray_x, tb_y + 8, tray_w, 32, GFX_COLOR_EMERALD_SURFACE);
    gfx_draw_rect_outline(surf, tray_x, tb_y + 8, tray_w, 32, GFX_COLOR_EMERALD_ACCENT);

    struct rtc_datetime rtc_now;
    char clock_str[32];
    if (rtc_get_datetime(&rtc_now)) {
        char time_buf[16];
        rtc_format_time(&rtc_now, time_buf, sizeof(time_buf));
        str_copy(clock_str, "[NET] [VOL] ", sizeof(clock_str));
        size_t c_len = str_length(clock_str);
        size_t t_idx = 0;
        while (time_buf[t_idx] != '\0' && c_len + 1 < sizeof(clock_str)) {
            clock_str[c_len++] = time_buf[t_idx++];
        }
        clock_str[c_len] = '\0';
    } else {
        str_copy(clock_str, "[NET] [VOL] 12:00", sizeof(clock_str));
    }
    gfx_draw_string(surf, tray_x + 8, tb_y + 16, clock_str, GFX_COLOR_TEXT_PRIMARY);

    /* Developer footer from the central metadata source. The domain is
     * underlined and click-handled above. */
    {
        const struct dawn_metadata_view *meta = dawn_metadata_get();
        if (meta != NULL) {
            uint32_t fx = footer_x(surf->width);
            uint32_t fy = tb_y + 16U;
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
        int32_t ly = (int32_t)(tb_y - 350);
        gfx_draw_glass_panel(surf, (uint32_t)lx, (uint32_t)ly, 220, 346, GFX_COLOR_GLASS_PANEL);

        /* Header */
        gfx_draw_icon(surf, (uint32_t)(lx + 12), (uint32_t)(ly + 10), GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 36), (uint32_t)(ly + 10), "DAWN-OS", GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_line(surf, lx + 8, ly + 32, lx + 212, ly + 32, GFX_COLOR_GLASS_BORDER);

        /* Menu Items */
        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 42), GFX_ICON_TERMINAL, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 42), "Terminal", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 70), GFX_ICON_FILE_EXPLORER, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 70), "File Explorer", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 98), GFX_ICON_TASK_MANAGER, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 98), "Task Manager", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 126), GFX_ICON_CALCULATOR, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 126), "Calculator", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 154), GFX_ICON_CALENDAR, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 154), "Calendar", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 182), GFX_ICON_SETTINGS, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 182), "Settings", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 210), GFX_ICON_SETTINGS, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 210), "Help & Support", GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 238), GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 238), "About DAWN-OS", GFX_COLOR_TEXT_PRIMARY);

        /* Shutdown button at bottom */
        gfx_draw_line(surf, lx + 8, ly + 270, lx + 212, ly + 270, GFX_COLOR_GLASS_BORDER);
        gfx_draw_icon(surf, (uint32_t)(lx + 16), (uint32_t)(ly + 288), GFX_ICON_SHUTDOWN, GFX_COLOR_BTN_CLOSE);
        gfx_draw_string(surf, (uint32_t)(lx + 40), (uint32_t)(ly + 288), "Shut Down", GFX_COLOR_TEXT_PRIMARY);
    }

    /* Render Notifications in top right */
    uint32_t notif_y = 16;
    for (size_t i = 0; i < DESKTOP_MAX_NOTIFICATIONS; i++) {
        if (state.notifications[i].active) {
            if (state.notifications[i].ticks_remaining > 0) {
                state.notifications[i].ticks_remaining--;
                if (state.notifications[i].ticks_remaining == 0) {
                    state.notifications[i].active = false;
                    continue;
                }
            }
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

    /* Input event tests */
    {
        uint32_t tb_y = state.screen_height - DESKTOP_TASKBAR_HEIGHT;

        /* Click start button */
        struct input_event click_start = {
            .type = INPUT_EVENT_MOUSE,
            .mouse = { .x = 20, .y = (int32_t)(tb_y + 16), .left_button = true }
        };
        desktop_handle_input_event(&click_start);
        if (!state.launcher_open) return false;

        struct input_event mouse_release = {
            .type = INPUT_EVENT_MOUSE,
            .mouse = { .x = 20, .y = (int32_t)(tb_y + 16), .left_button = false }
        };
        desktop_handle_input_event(&mouse_release);

        /* Click About DAWN-OS launcher item (y = tb_y - 290 + 200 = tb_y - 90) */
        struct input_event click_about = {
            .type = INPUT_EVENT_MOUSE,
            .mouse = { .x = 50, .y = (int32_t)(tb_y - 90), .left_button = true }
        };
        desktop_handle_input_event(&click_about);
        if (state.launcher_open) return false;
        if (settings_window_get_id() < 0) return false;
        settings_window_close();

        mouse_release.mouse.x = 50;
        mouse_release.mouse.y = (int32_t)(tb_y - 90);
        desktop_handle_input_event(&mouse_release);

        /* Click desktop icon "This PC" (x = 30, y = 35) */
        struct input_event click_thispc = {
            .type = INPUT_EVENT_MOUSE,
            .mouse = { .x = 30, .y = 35, .left_button = true }
        };
        desktop_handle_input_event(&click_thispc);
        if (settings_window_get_id() < 0) return false;
        settings_window_close();

        mouse_release.mouse.x = 30;
        mouse_release.mouse.y = 35;
        desktop_handle_input_event(&mouse_release);
    }

    /* Keyboard-first launcher navigation tests */
    {
        /* 1. Super key toggles launcher open */
        struct input_event super_press = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x5BU,
                .keycode = KEY_SUPER,
                .pressed = true,
                .shift = false,
                .ctrl = false,
                .alt = false,
                .gui = true
            }
        };
        desktop_handle_input_event(&super_press);
        if (!state.launcher_open) return false;

        /* 2. Escape key dismisses launcher */
        struct input_event esc_press = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x01U,
                .keycode = KEY_ESCAPE,
                .pressed = true,
                .shift = false,
                .ctrl = false,
                .alt = false,
                .gui = false
            }
        };
        desktop_handle_input_event(&esc_press);
        if (state.launcher_open) return false;

        /* 3. Ctrl+Escape opens launcher */
        struct input_event ctrl_esc = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x01U,
                .keycode = KEY_ESCAPE,
                .pressed = true,
                .shift = false,
                .ctrl = true,
                .alt = false,
                .gui = false
            }
        };
        desktop_handle_input_event(&ctrl_esc);
        if (!state.launcher_open) return false;

        /* 4. Super key toggles launcher closed */
        desktop_handle_input_event(&super_press);
        if (state.launcher_open) return false;
    }

    /* Workspace switching interaction tests */
    {
        /* 1. Mouse click on Workspace 2 pill in top status bar (x = 96 + 28 + 4 = 128, y = 10) */
        struct input_event click_ws = {
            .type = INPUT_EVENT_MOUSE,
            .mouse = {
                .x = 128,
                .y = 10,
                .left_button = true,
                .right_button = false,
                .middle_button = false,
                .dx = 0,
                .dy = 0
            }
        };
        desktop_handle_input_event(&click_ws);
        if (desktop_get_workspace() != 1U) return false;

        click_ws.mouse.left_button = false;
        desktop_handle_input_event(&click_ws);

        /* 2. Keyboard shortcut Super + 1 returns to Workspace 0 */
        struct input_event super_1 = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x02U,
                .keycode = KEY_1,
                .pressed = true,
                .shift = false,
                .ctrl = false,
                .alt = false,
                .gui = true
            }
        };
        desktop_handle_input_event(&super_1);
        if (desktop_get_workspace() != 0U) return false;

        /* 3. Keyboard shortcut Ctrl + Alt + Right switches to Workspace 1 */
        struct input_event ctrl_alt_right = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x4DU,
                .keycode = KEY_RIGHT,
                .pressed = true,
                .shift = false,
                .ctrl = true,
                .alt = true,
                .gui = false
            }
        };
        desktop_handle_input_event(&ctrl_alt_right);
        if (desktop_get_workspace() != 1U) return false;

        /* 4. Keyboard shortcut Ctrl + Alt + Left switches back to Workspace 0 */
        struct input_event ctrl_alt_left = {
            .type = INPUT_EVENT_KEYBOARD,
            .key = {
                .scancode = 0x4BU,
                .keycode = KEY_LEFT,
                .pressed = true,
                .shift = false,
                .ctrl = true,
                .alt = true,
                .gui = false
            }
        };
        desktop_handle_input_event(&ctrl_alt_left);
        if (desktop_get_workspace() != 0U) return false;
    }

    /* Damage tracking & bounded redraw scheduled test */
    {
        gfx_damage_clear();
        if (gfx_damage_is_dirty()) return false;

        desktop_switch_workspace(2U);
        if (!gfx_damage_is_dirty()) return false;

        struct gfx_rect bounds = gfx_damage_get_bounds();
        if (bounds.width == 0U || bounds.height == 0U) return false;

        gfx_flush();
        if (gfx_damage_is_dirty()) return false;

        desktop_switch_workspace(0U);
    }

    /* Render test into a 640x480 test surface */
    struct gfx_surface test_surf = {
        .buffer = test_render_buf,
        .width = 640,
        .height = 480,
        .pitch = 640 * sizeof(uint32_t)
    };

    desktop_render(&test_surf);

    /* Verify top status bar was drawn (neon lower accent line at y = 28 - 1 = 27) */
    uint32_t sb_line_pixel = test_surf.buffer[(DESKTOP_STATUSBAR_HEIGHT - 1U) * 640 + 10];
    if (sb_line_pixel != GFX_COLOR_EMERALD_ACCENT) return false;

    /* Verify status bar body is non-zero */
    uint32_t sb_body_pixel = test_surf.buffer[10 * 640 + 10];
    if (sb_body_pixel == 0) return false;

    /* Verify taskbar was drawn (check bottom pixels not all zero) */
    uint32_t tb_pixel = test_surf.buffer[(480 - 10) * 640 + 20];
    if (tb_pixel == 0) return false;

    /* Verify top neon accent line was drawn at y = 480 - 48 = 432 */
    uint32_t top_line_pixel = test_surf.buffer[432 * 640 + 10];
    if (top_line_pixel != GFX_COLOR_EMERALD_ACCENT) return false;

    /* Verify synchronized screen dimensions */
    if (state.screen_width != 640 || state.screen_height != 480) return false;

    /* Verify system tray container was drawn */
    uint32_t tray_x = 640U - 176U - 8U;
    uint32_t tray_pixel = test_surf.buffer[(432 + 8) * 640 + tray_x + 10];
    if (tray_pixel == 0) return false;

    return true;
}
