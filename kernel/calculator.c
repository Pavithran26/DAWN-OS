/* SPDX-License-Identifier: Unlicense */
#include "calculator.h"
#include "gfx.h"
#include "window_manager.h"

static uint32_t calc_win_buffer[CALCULATOR_WIDTH * CALCULATOR_HEIGHT];
static int calc_window_id = -1;

static int64_t current_val = 0;
static int64_t stored_val = 0;
static char current_op = '\0';
static bool new_entry = true;
static bool error_state = false;
static char display_str[32] = "0";

static void u64_to_str_local(uint64_t val, char *buf) {
    char tmp[24];
    size_t i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    for (size_t j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[i] = '\0';
}

static void i64_to_str_local(int64_t val, char *buf) {
    if (val < 0) {
        buf[0] = '-';
        u64_to_str_local((uint64_t)(-val), buf + 1);
    } else {
        u64_to_str_local((uint64_t)val, buf);
    }
}

static size_t str_len_local(const char *s) {
    size_t len = 0;
    while (s != NULL && s[len] != '\0') len++;
    return len;
}

static void str_copy_local(char *dst, const char *src, size_t max_len) {
    size_t i = 0;
    while (src != NULL && src[i] != '\0' && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void calculator_compute(void) {
    if (error_state || current_op == '\0') return;

    int64_t res = 0;
    switch (current_op) {
        case '+':
            res = stored_val + current_val;
            break;
        case '-':
            res = stored_val - current_val;
            break;
        case '*':
            res = stored_val * current_val;
            break;
        case '/':
            if (current_val == 0) {
                error_state = true;
                str_copy_local(display_str, "Error", sizeof(display_str));
                return;
            }
            res = stored_val / current_val;
            break;
        case '%':
            if (current_val == 0) {
                error_state = true;
                str_copy_local(display_str, "Error", sizeof(display_str));
                return;
            }
            res = stored_val % current_val;
            break;
        default:
            return;
    }

    current_val = res;
    stored_val = res;
    i64_to_str_local(current_val, display_str);
}

static void calculator_press_digit(int digit) {
    if (error_state) {
        error_state = false;
        current_val = 0;
        stored_val = 0;
        current_op = '\0';
    }

    if (new_entry) {
        current_val = digit;
        display_str[0] = (char)('0' + digit);
        display_str[1] = '\0';
        new_entry = false;
    } else {
        size_t len = str_len_local(display_str);
        if (len < 12U) {
            if (len == 1U && display_str[0] == '0') {
                display_str[0] = (char)('0' + digit);
            } else {
                display_str[len] = (char)('0' + digit);
                display_str[len + 1] = '\0';
            }
            current_val = current_val * 10 + digit;
        }
    }
}

static void calculator_press_op(char op) {
    if (error_state) return;

    if (current_op != '\0' && !new_entry) {
        calculator_compute();
    } else {
        stored_val = current_val;
    }

    current_op = op;
    new_entry = true;
}

static void calculator_press_equals(void) {
    if (error_state || current_op == '\0') return;
    calculator_compute();
    current_op = '\0';
    new_entry = true;
}

static void calculator_press_clear(void) {
    current_val = 0;
    stored_val = 0;
    current_op = '\0';
    new_entry = true;
    error_state = false;
    str_copy_local(display_str, "0", sizeof(display_str));
}

static void calculator_press_negate(void) {
    if (error_state) return;
    current_val = -current_val;
    i64_to_str_local(current_val, display_str);
}

bool calculator_initialize(void) {
    calculator_press_clear();
    return true;
}

void calculator_window_render(void) {
    if (calc_window_id < 0) return;
    struct window *win = wm_get_window_by_id((uint32_t)calc_window_id);
    if (win == NULL || win->client_buffer == NULL) return;

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

    /* Display Screen (Frosted glass) */
    gfx_draw_glass_panel(&surf, 12, 10, cw - 24U, 46, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, 12, 10, cw - 24U, 46, GFX_COLOR_EMERALD_ACCENT);

    /* Operator label in upper display corner */
    if (current_op != '\0') {
        char op_str[4] = { current_op, ' ', '\0', '\0' };
        gfx_draw_string(&surf, cw - 36U, 14, op_str, GFX_COLOR_EMERALD_NEON);
    }

    /* Right-aligned text */
    size_t text_len = str_len_local(display_str);
    uint32_t text_width = (uint32_t)(text_len * 8U);
    uint32_t text_x = (cw - 28U > text_width) ? (cw - 28U - text_width) : 18U;
    gfx_draw_string(&surf, text_x, 30, display_str, GFX_COLOR_TEXT_PRIMARY);

    /* Button Grid Setup */
    struct calc_btn {
        const char *label;
        uint32_t color;
        bool is_op;
    };

    static const struct calc_btn buttons[5][4] = {
        { { "C", GFX_COLOR_BTN_CLOSE, true }, { "+/-", GFX_COLOR_EMERALD_BASE, true }, { "%", GFX_COLOR_EMERALD_BASE, true }, { "/", GFX_COLOR_EMERALD_PRIMARY, true } },
        { { "7", GFX_COLOR_EMERALD_SURFACE, false }, { "8", GFX_COLOR_EMERALD_SURFACE, false }, { "9", GFX_COLOR_EMERALD_SURFACE, false }, { "*", GFX_COLOR_EMERALD_PRIMARY, true } },
        { { "4", GFX_COLOR_EMERALD_SURFACE, false }, { "5", GFX_COLOR_EMERALD_SURFACE, false }, { "6", GFX_COLOR_EMERALD_SURFACE, false }, { "-", GFX_COLOR_EMERALD_PRIMARY, true } },
        { { "1", GFX_COLOR_EMERALD_SURFACE, false }, { "2", GFX_COLOR_EMERALD_SURFACE, false }, { "3", GFX_COLOR_EMERALD_SURFACE, false }, { "+", GFX_COLOR_EMERALD_PRIMARY, true } },
        { { "0", GFX_COLOR_EMERALD_SURFACE, false }, { " ", GFX_COLOR_EMERALD_SURFACE, false }, { ".", GFX_COLOR_EMERALD_BASE, false }, { "=", GFX_COLOR_EMERALD_HIGHLIGHT, true } }
    };

    uint32_t start_y = 66;
    uint32_t btn_h = 36;
    uint32_t gap_y = 8;
    uint32_t btn_w = 46;
    uint32_t gap_x = 8;
    uint32_t start_x = 14;

    for (int r = 0; r < 5; r++) {
        uint32_t by = start_y + (uint32_t)r * (btn_h + gap_y);
        for (int c = 0; c < 4; c++) {
            if (r == 4 && c == 1) continue; /* 0 spans across columns 0 and 1 */

            uint32_t bx = start_x + (uint32_t)c * (btn_w + gap_x);
            uint32_t bw = (r == 4 && c == 0) ? (btn_w * 2U + gap_x) : btn_w;

            const struct calc_btn *b = &buttons[r][c];
            gfx_draw_glass_panel(&surf, bx, by, bw, btn_h, b->color);
            gfx_draw_rect_outline(&surf, bx, by, bw, btn_h, GFX_COLOR_EMERALD_ACCENT);

            /* Center label */
            size_t lbl_len = str_len_local(b->label);
            uint32_t lx = bx + (bw - (uint32_t)(lbl_len * 8U)) / 2U;
            uint32_t ly = by + (btn_h - 16U) / 2U;
            gfx_draw_string(&surf, lx, ly, b->label, GFX_COLOR_TEXT_PRIMARY);
        }
    }
}

void calculator_window_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x < 0 || local_y < 0) return;

    uint32_t start_y = 66;
    uint32_t btn_h = 36;
    uint32_t gap_y = 8;
    uint32_t btn_w = 46;
    uint32_t gap_x = 8;
    uint32_t start_x = 14;

    for (int r = 0; r < 5; r++) {
        int32_t by = (int32_t)(start_y + (uint32_t)r * (btn_h + gap_y));
        if (local_y >= by && local_y < (int32_t)(by + (int32_t)btn_h)) {
            for (int c = 0; c < 4; c++) {
                if (r == 4 && c == 1) continue;

                int32_t bx = (int32_t)(start_x + (uint32_t)c * (btn_w + gap_x));
                int32_t bw = (int32_t)((r == 4 && c == 0) ? (btn_w * 2U + gap_x) : btn_w);

                if (local_x >= bx && local_x < bx + bw) {
                    /* Button clicked! */
                    if (r == 0) {
                        if (c == 0) calculator_press_clear();
                        else if (c == 1) calculator_press_negate();
                        else if (c == 2) calculator_press_op('%');
                        else if (c == 3) calculator_press_op('/');
                    } else if (r == 1) {
                        if (c == 0) calculator_press_digit(7);
                        else if (c == 1) calculator_press_digit(8);
                        else if (c == 2) calculator_press_digit(9);
                        else if (c == 3) calculator_press_op('*');
                    } else if (r == 2) {
                        if (c == 0) calculator_press_digit(4);
                        else if (c == 1) calculator_press_digit(5);
                        else if (c == 2) calculator_press_digit(6);
                        else if (c == 3) calculator_press_op('-');
                    } else if (r == 3) {
                        if (c == 0) calculator_press_digit(1);
                        else if (c == 1) calculator_press_digit(2);
                        else if (c == 2) calculator_press_digit(3);
                        else if (c == 3) calculator_press_op('+');
                    } else if (r == 4) {
                        if (c == 0) calculator_press_digit(0);
                        else if (c == 3) calculator_press_equals();
                    }
                    calculator_window_render();
                    return;
                }
            }
        }
    }
}

static void calculator_on_window_close(struct window *win) {
    (void)win;
    calc_window_id = -1;
}

bool calculator_window_open(void) {
    if (calc_window_id >= 0) {
        if (wm_get_window_by_id((uint32_t)calc_window_id) != NULL) {
            wm_focus_window((uint32_t)calc_window_id);
            return true;
        }
        calc_window_id = -1;
    }

    struct window *win = wm_create_window("Calculator", 160, 100, CALCULATOR_WIDTH, CALCULATOR_HEIGHT,
                                          WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR);
    if (win == NULL) return false;

    win->client_buffer = calc_win_buffer;
    win->client_pitch = CALCULATOR_WIDTH * sizeof(uint32_t);
    win->on_click = calculator_window_handle_click;
    win->on_close = calculator_on_window_close;
    calc_window_id = (int)win->id;

    calculator_window_render();
    return true;
}

void calculator_window_close(void) {
    if (calc_window_id >= 0) {
        wm_destroy_window((uint32_t)calc_window_id);
        calc_window_id = -1;
    }
}

#ifdef DAWN_KERNEL_TEST
bool calculator_self_test(void) {
    calculator_initialize();

    /* Test: 12 + 34 = 46 */
    calculator_press_digit(1);
    calculator_press_digit(2);
    if (current_val != 12) return false;

    calculator_press_op('+');
    if (stored_val != 12 || current_op != '+') return false;

    calculator_press_digit(3);
    calculator_press_digit(4);
    if (current_val != 34) return false;

    calculator_press_equals();
    if (current_val != 46 || str_len_local(display_str) == 0) return false;

    /* Test: Divide by Zero */
    calculator_press_op('/');
    calculator_press_digit(0);
    calculator_press_equals();
    if (!error_state) return false;

    /* Clear */
    calculator_press_clear();
    if (error_state || current_val != 0 || current_op != '\0') return false;

    return true;
}
#endif
