/* SPDX-License-Identifier: Unlicense */
#include "calendar.h"
#include "gfx.h"
#include "rtc.h"
#include "window_manager.h"

static uint32_t cal_win_buffer[CALENDAR_WIDTH * CALENDAR_HEIGHT];
static int cal_window_id = -1;

static uint16_t displayed_year = 2026;
static uint8_t displayed_month = 11;
static struct rtc_datetime today = {
    .second = 0,
    .minute = 0,
    .hour = 12,
    .day = 19,
    .month = 11,
    .year = 2026
};

static const char *const month_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *const day_headers[7] = {
    "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"
};

static size_t str_len_local(const char *s) {
    size_t len = 0;
    while (s != NULL && s[len] != '\0') len++;
    return len;
}

static bool is_leap_year(uint16_t year) {
    return (year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U));
}

static uint8_t get_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1U || month > 12U) return 30U;
    if (month == 2U && is_leap_year(year)) return 29U;
    return days[month - 1U];
}

static uint8_t get_first_day_of_week(uint16_t y, uint8_t m) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int year = (int)y;
    if (m < 3U) year -= 1;
    int d = (year + year / 4 - year / 100 + year / 400 + t[m - 1U] + 1) % 7;
    return (uint8_t)((d < 0) ? (d + 7) : d);
}

bool calendar_initialize(void) {
    if (rtc_get_datetime(&today)) {
        displayed_year = today.year;
        displayed_month = today.month;
    } else {
        displayed_year = 2026;
        displayed_month = 11;
    }
    return true;
}

void calendar_window_render(void) {
    if (cal_window_id < 0) return;
    struct window *win = wm_get_window_by_id((uint32_t)cal_window_id);
    if (win == NULL || win->client_buffer == NULL) return;

    /* Refresh live RTC date */
    (void)rtc_get_datetime(&today);

    uint32_t cw = win->width - (2U * WM_BORDER_WIDTH);
    uint32_t ch = win->height - WM_TITLEBAR_HEIGHT - (2U * WM_BORDER_WIDTH);

    struct gfx_surface surf = {
        .buffer = win->client_buffer,
        .width = cw,
        .height = ch,
        .pitch = win->client_pitch
    };

    /* Background panel */
    gfx_draw_gradient_v(&surf, 0, 0, cw, ch, GFX_COLOR_EMERALD_BASE, GFX_COLOR_EMERALD_DEEP);

    /* Navigation buttons: [<] and [>] */
    gfx_draw_glass_panel(&surf, 12, 10, 28, 24, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, 12, 10, 28, 24, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(&surf, 22, 14, "<", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_glass_panel(&surf, cw - 40U, 10, 28, 24, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, cw - 40U, 10, 28, 24, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(&surf, cw - 30U, 14, ">", GFX_COLOR_TEXT_PRIMARY);

    /* Title: Month and Year (e.g. "November 2026") */
    char title_buf[32] = {0};
    const char *mname = (displayed_month >= 1U && displayed_month <= 12U) ? month_names[displayed_month - 1U] : "Month";
    size_t ti = 0;
    while (mname[ti] != '\0' && ti < 16U) {
        title_buf[ti] = mname[ti];
        ti++;
    }
    title_buf[ti++] = ' ';
    uint16_t yr = displayed_year;
    title_buf[ti++] = (char)('0' + ((yr / 1000U) % 10U));
    title_buf[ti++] = (char)('0' + ((yr / 100U) % 10U));
    title_buf[ti++] = (char)('0' + ((yr / 10U) % 10U));
    title_buf[ti++] = (char)('0' + (yr % 10U));
    title_buf[ti] = '\0';

    uint32_t t_width = (uint32_t)(ti * 8U);
    uint32_t t_x = (cw > t_width) ? ((cw - t_width) / 2U) : 50U;
    gfx_draw_string(&surf, t_x, 14, title_buf, GFX_COLOR_EMERALD_NEON);

    /* Sub-bar: Today's live date and time */
    char today_buf[40] = "Today: ";
    size_t tdi = 7;
    const char *tmname = (today.month >= 1U && today.month <= 12U) ? month_names[today.month - 1U] : "Month";
    for (size_t mi = 0; mi < 3 && tmname[mi] != '\0'; mi++) {
        today_buf[tdi++] = tmname[mi];
    }
    today_buf[tdi++] = ' ';
    today_buf[tdi++] = (char)('0' + (today.day / 10U));
    today_buf[tdi++] = (char)('0' + (today.day % 10U));
    today_buf[tdi++] = ' ';
    today_buf[tdi++] = ' ';
    char live_time[16] = {0};
    rtc_format_time(&today, live_time, sizeof(live_time));
    for (size_t lti = 0; live_time[lti] != '\0' && tdi < 38U; lti++) {
        today_buf[tdi++] = live_time[lti];
    }
    today_buf[tdi] = '\0';

    uint32_t sub_w = (uint32_t)(tdi * 8U);
    uint32_t sub_x = (cw > sub_w) ? ((cw - sub_w) / 2U) : 40U;
    gfx_draw_string(&surf, sub_x, 40, today_buf, GFX_COLOR_TEXT_MUTED);

    /* Divider line */
    gfx_draw_line(&surf, 12, 58, (int32_t)(cw - 12U), 58, GFX_COLOR_EMERALD_BASE);

    /* Day of week headers */
    uint32_t col_w = (cw - 24U) / 7U;
    uint32_t start_x = 12U;
    uint32_t header_y = 66U;

    for (uint8_t d = 0; d < 7U; d++) {
        uint32_t hx = start_x + (uint32_t)d * col_w + (col_w - 16U) / 2U;
        uint32_t hcolor = (d == 0U || d == 6U) ? GFX_COLOR_EMERALD_HIGHLIGHT : GFX_COLOR_TEXT_SECONDARY;
        gfx_draw_string(&surf, hx, header_y, day_headers[d], hcolor);
    }

    /* Grid of days */
    uint8_t first_day = get_first_day_of_week(displayed_year, displayed_month);
    uint8_t total_days = get_days_in_month(displayed_year, displayed_month);

    uint8_t current_col = first_day;
    uint32_t current_row_y = 90U;
    uint32_t row_h = 26U;

    for (uint8_t day_num = 1; day_num <= total_days; day_num++) {
        uint32_t cell_x = start_x + (uint32_t)current_col * col_w;
        uint32_t cell_y = current_row_y;

        bool is_current_today = (displayed_year == today.year &&
                                 displayed_month == today.month &&
                                 day_num == today.day);

        if (is_current_today) {
            /* Highlight current day pill */
            gfx_draw_glass_panel(&surf, cell_x + 2U, cell_y - 2U, col_w - 4U, row_h - 2U, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_rect_outline(&surf, cell_x + 2U, cell_y - 2U, col_w - 4U, row_h - 2U, GFX_COLOR_EMERALD_NEON);
        }

        char num_str[4];
        if (day_num < 10U) {
            num_str[0] = (char)('0' + day_num);
            num_str[1] = '\0';
        } else {
            num_str[0] = (char)('0' + (day_num / 10U));
            num_str[1] = (char)('0' + (day_num % 10U));
            num_str[2] = '\0';
        }

        uint32_t num_w = (uint32_t)(str_len_local(num_str) * 8U);
        uint32_t nx = cell_x + (col_w - num_w) / 2U;
        uint32_t ny = cell_y + 2U;

        uint32_t text_color = is_current_today ? GFX_COLOR_WHITE :
                              ((current_col == 0U || current_col == 6U) ? GFX_COLOR_EMERALD_HIGHLIGHT : GFX_COLOR_TEXT_PRIMARY);

        gfx_draw_string(&surf, nx, ny, num_str, text_color);

        current_col++;
        if (current_col == 7U) {
            current_col = 0U;
            current_row_y += row_h;
        }
    }
}

void calendar_window_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x < 0 || local_y < 0) return;

    /* Previous Month Button: [<] (x: 12..40, y: 10..34) */
    if (local_x >= 12 && local_x <= 40 && local_y >= 10 && local_y <= 34) {
        if (displayed_month == 1U) {
            displayed_month = 12U;
            if (displayed_year > 1970U) displayed_year--;
        } else {
            displayed_month--;
        }
        calendar_window_render();
        return;
    }

    /* Next Month Button: [>] (x: cw - 40 .. cw - 12, y: 10..34) */
    if (cal_window_id >= 0) {
        struct window *win = wm_get_window_by_id((uint32_t)cal_window_id);
        if (win != NULL) {
            uint32_t cw = win->width - (2U * WM_BORDER_WIDTH);
            if (local_x >= (int32_t)(cw - 40U) && local_x <= (int32_t)(cw - 12U) &&
                local_y >= 10 && local_y <= 34) {
                if (displayed_month == 12U) {
                    displayed_month = 1U;
                    displayed_year++;
                } else {
                    displayed_month++;
                }
                calendar_window_render();
                return;
            }
        }
    }

    /* Sub-bar click ("Today") to reset back to current date */
    if (local_y >= 36 && local_y <= 54) {
        (void)rtc_get_datetime(&today);
        displayed_year = today.year;
        displayed_month = today.month;
        calendar_window_render();
        return;
    }
}

static void calendar_on_window_close(struct window *win) {
    (void)win;
    cal_window_id = -1;
}

bool calendar_window_open(void) {
    if (cal_window_id >= 0) {
        if (wm_get_window_by_id((uint32_t)cal_window_id) != NULL) {
            wm_focus_window((uint32_t)cal_window_id);
            return true;
        }
        cal_window_id = -1;
    }

    calendar_initialize();

    struct window *win = wm_create_window("Calendar", 220, 120, CALENDAR_WIDTH, CALENDAR_HEIGHT,
                                          WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR);
    if (win == NULL) return false;

    win->client_buffer = cal_win_buffer;
    win->client_pitch = CALENDAR_WIDTH * sizeof(uint32_t);
    win->on_click = calendar_window_handle_click;
    win->on_close = calendar_on_window_close;
    cal_window_id = (int)win->id;

    calendar_window_render();
    return true;
}

void calendar_window_close(void) {
    if (cal_window_id >= 0) {
        wm_destroy_window((uint32_t)cal_window_id);
        cal_window_id = -1;
    }
}

#ifdef DAWN_KERNEL_TEST
bool calendar_self_test(void) {
    /* Leap year verification */
    if (!is_leap_year(2024U)) return false;
    if (is_leap_year(2025U)) return false;
    if (is_leap_year(2100U)) return false;
    if (!is_leap_year(2000U)) return false;

    /* Days in month verification */
    if (get_days_in_month(2024U, 2U) != 29U) return false;
    if (get_days_in_month(2025U, 2U) != 28U) return false;
    if (get_days_in_month(2026U, 11U) != 30U) return false;
    if (get_days_in_month(2026U, 12U) != 31U) return false;

    /* First day of week calculation */
    /* 2026-11-01 was a Sunday (0) */
    if (get_first_day_of_week(2026U, 11U) != 0U) return false;

    return true;
}
#endif
