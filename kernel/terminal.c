/* SPDX-License-Identifier: Unlicense */
#include "terminal.h"

static struct terminal main_terminal = {0};
static bool term_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

static inline void u64_to_str(uint64_t val, char *buf) {
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

static void print_prompt(struct terminal *term) {
    terminal_write(term, "\033[32madmin@dawn\033[0m:");
    terminal_write(term, term->shell.cwd);
    terminal_write(term, "$ ");
}

static void scroll(struct terminal *term) {
    /* Push row 0 into scrollback circular ring buffer */
    uint32_t sb_idx;
    if (term->scrollback_count < TERM_SCROLLBACK_ROWS) {
        sb_idx = (term->scrollback_head + term->scrollback_count) % TERM_SCROLLBACK_ROWS;
        term->scrollback_count++;
    } else {
        sb_idx = term->scrollback_head;
        term->scrollback_head = (term->scrollback_head + 1U) % TERM_SCROLLBACK_ROWS;
    }
    for (uint32_t c = 0; c < TERM_COLS; c++) {
        term->scrollback[sb_idx][c] = term->grid[0][c];
    }

    /* Shift live grid rows up */
    for (uint32_t r = 0; r + 1 < TERM_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_COLS; c++) {
            term->grid[r][c] = term->grid[r + 1][c];
        }
    }
    for (uint32_t c = 0; c < TERM_COLS; c++) {
        term->grid[TERM_ROWS - 1][c] = (struct term_cell){' ', term->current_fg, term->current_bg};
    }
    term->cursor_row = TERM_ROWS - 1;
}

static struct term_cell get_visible_cell(const struct terminal *term, uint32_t r, uint32_t c) {
    if (r >= TERM_ROWS || c >= TERM_COLS) {
        return (struct term_cell){' ', term->current_fg, term->current_bg};
    }

    if (term->scroll_offset == 0U) {
        return term->grid[r][c];
    }

    uint32_t offset = (term->scroll_offset <= term->scrollback_count) ? term->scroll_offset : term->scrollback_count;
    uint32_t v = (term->scrollback_count + r) - offset;

    if (v < term->scrollback_count) {
        uint32_t ring_idx = (term->scrollback_head + v) % TERM_SCROLLBACK_ROWS;
        return term->scrollback[ring_idx][c];
    } else {
        uint32_t grid_r = v - term->scrollback_count;
        if (grid_r < TERM_ROWS) {
            return term->grid[grid_r][c];
        }
    }

    return (struct term_cell){' ', term->current_fg, term->current_bg};
}

void terminal_clear(struct terminal *term) {
    if (term == NULL) return;
    for (uint32_t r = 0; r < TERM_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_COLS; c++) {
            term->grid[r][c] = (struct term_cell){' ', term->current_fg, term->current_bg};
        }
    }
    term->cursor_col = 0;
    term->cursor_row = 0;
    term->scroll_offset = 0;
}

void terminal_put_char(struct terminal *term, char c) {
    if (term == NULL) return;

    if (c == '\n') {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= TERM_ROWS) scroll(term);
        return;
    }

    if (c == '\r') {
        term->cursor_col = 0;
        return;
    }

    if (c == '\b') {
        if (term->cursor_col > 0) {
            term->cursor_col--;
            term->grid[term->cursor_row][term->cursor_col] = (struct term_cell){' ', term->current_fg, term->current_bg};
        } else if (term->cursor_row > 0) {
            term->cursor_row--;
            term->cursor_col = TERM_COLS - 1U;
            term->grid[term->cursor_row][term->cursor_col] = (struct term_cell){' ', term->current_fg, term->current_bg};
        }
        return;
    }

    if (c == '\t') {
        uint32_t tab_stop = (term->cursor_col + 4U) & ~3U;
        while (term->cursor_col < tab_stop && term->cursor_col < TERM_COLS) {
            terminal_put_char(term, ' ');
        }
        return;
    }

    if (term->cursor_col >= TERM_COLS) {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= TERM_ROWS) scroll(term);
    }

    term->grid[term->cursor_row][term->cursor_col] = (struct term_cell){c, term->current_fg, term->current_bg};
    term->cursor_col++;
}

void terminal_write(struct terminal *term, const char *str) {
    if (term == NULL || str == NULL) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        /* Bounded ANSI CSI sequence parser \033[... */
        if (str[i] == '\033' && str[i + 1] == '[') {
            size_t start = i + 2;
            size_t end = start;
            while (str[end] != '\0' && ((str[end] >= '0' && str[end] <= '9') || str[end] == ';') && (end - start < 16)) {
                end++;
            }
            char cmd = str[end];
            if (cmd == 'm') {
                /* SGR Color / Attribute */
                if (end == start || (str[start] == '0' && end == start + 1)) {
                    term->current_fg = GFX_COLOR_TEXT_PRIMARY;
                    term->current_bg = GFX_COLOR_TRANSPARENT;
                } else if (str[start] == '1' && end == start + 1) {
                    term->current_fg = GFX_COLOR_EMERALD_HIGHLIGHT;
                } else if (str[start] == '3' && end == start + 2) {
                    char col = str[start + 1];
                    if (col == '1') term->current_fg = 0xFFEF4444U; /* Red */
                    else if (col == '2') term->current_fg = GFX_COLOR_EMERALD_ACCENT; /* Green */
                    else if (col == '3') term->current_fg = 0xFFF59E0BU; /* Amber */
                    else if (col == '4') term->current_fg = 0xFF38BDF8U; /* Blue */
                    else if (col == '6') term->current_fg = GFX_COLOR_EMERALD_CYAN; /* Cyan */
                    else if (col == '7') term->current_fg = GFX_COLOR_WHITE; /* White */
                    else term->current_fg = GFX_COLOR_TEXT_PRIMARY;
                }
                i = end;
                continue;
            } else if (cmd == 'J') {
                /* Clear display */
                terminal_clear(term);
                i = end;
                continue;
            } else if (cmd == 'H') {
                /* Cursor home */
                term->cursor_col = 0;
                term->cursor_row = 0;
                i = end;
                continue;
            } else if (cmd == 'K') {
                /* Clear line from cursor to right border */
                for (uint32_t c = term->cursor_col; c < TERM_COLS; c++) {
                    term->grid[term->cursor_row][c] = (struct term_cell){' ', term->current_fg, term->current_bg};
                }
                i = end;
                continue;
            }
        }
        terminal_put_char(term, str[i]);
    }
}

void terminal_render(struct terminal *term) {
    if (term == NULL) return;

    struct gfx_surface surf = {
        .buffer = term->render_buffer,
        .width = TERM_PIXEL_WIDTH,
        .height = TERM_PIXEL_HEIGHT,
        .pitch = TERM_PIXEL_WIDTH * sizeof(uint32_t)
    };

    gfx_clear(&surf, GFX_COLOR_TRANSPARENT);

    for (uint32_t r = 0; r < TERM_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_COLS; c++) {
            struct term_cell cell = get_visible_cell(term, r, c);
            uint32_t px = c * 8U;
            uint32_t py = r * 16U;

            /* Draw cell background if not transparent */
            if ((cell.bg >> 24) != 0U) {
                gfx_draw_rect_filled(&surf, px, py, 8, 16, cell.bg);
            }

            /* Draw glyph */
            if (cell.ch != ' ') {
                gfx_draw_char(&surf, px, py, cell.ch, cell.fg);
            }
        }
    }

    /* Render cursor if visible and in current viewport */
    if (term->cursor_visible && term->cursor_col < TERM_COLS) {
        if (term->scroll_offset == 0U && term->cursor_row < TERM_ROWS) {
            uint32_t cx = term->cursor_col * 8U;
            uint32_t cy = term->cursor_row * 16U;
            gfx_draw_rect_filled(&surf, cx, cy + 13, 8, 3, GFX_COLOR_EMERALD_ACCENT);
        }
    }

    /* Render glass scroll indicator badge on top-right if viewing scrollback */
    if (term->scroll_offset > 0U) {
        char sb_info[32];
        sb_info[0] = '[';
        sb_info[1] = '^';
        sb_info[2] = ' ';
        char num[16];
        u64_to_str(term->scroll_offset, num);
        size_t nlen = str_len(num);
        for (size_t ni = 0; ni < nlen; ni++) {
            sb_info[3 + ni] = num[ni];
        }
        sb_info[3 + nlen] = ']';
        sb_info[4 + nlen] = '\0';
        uint32_t badge_w = (uint32_t)((4 + nlen) * 8U + 12U);
        uint32_t badge_x = (TERM_PIXEL_WIDTH > badge_w + 6U) ? (TERM_PIXEL_WIDTH - badge_w - 6U) : 0U;
        gfx_draw_glass_panel(&surf, badge_x, 4U, badge_w, 20U, GFX_COLOR_EMERALD_SURFACE);
        gfx_draw_rect_outline(&surf, badge_x, 4U, badge_w, 20U, GFX_COLOR_EMERALD_ACCENT);
        gfx_draw_string(&surf, badge_x + 6U, 6U, sb_info, GFX_COLOR_EMERALD_ACCENT);
    }
}

void terminal_scroll(struct terminal *term, int32_t delta) {
    if (term == NULL) return;
    int32_t new_offset = (int32_t)term->scroll_offset + delta;
    if (new_offset < 0) {
        new_offset = 0;
    } else if (new_offset > (int32_t)term->scrollback_count) {
        new_offset = (int32_t)term->scrollback_count;
    }
    term->scroll_offset = (uint32_t)new_offset;
}

void terminal_scroll_to_bottom(struct terminal *term) {
    if (term == NULL) return;
    term->scroll_offset = 0U;
}

uint32_t terminal_get_scroll_offset(const struct terminal *term) {
    return term ? term->scroll_offset : 0U;
}

uint32_t terminal_get_scrollback_count(const struct terminal *term) {
    return term ? term->scrollback_count : 0U;
}

static void terminal_on_window_key(struct window *win, const struct key_event *kevt) {
    (void)win;
    struct terminal *term = terminal_get_instance();
    terminal_handle_key(term, kevt);
}

static void terminal_on_window_close(struct window *win) {
    (void)win;
    struct terminal *term = terminal_get_instance();
    term->win = NULL;
}

void terminal_handle_key(struct terminal *term, const struct key_event *kevt) {
    if (term == NULL || kevt == NULL || !kevt->pressed) return;

    /* Shift + Navigation Keys: Scroll Viewport */
    if (kevt->shift) {
        if (kevt->keycode == KEY_UP) {
            terminal_scroll(term, 1);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
        if (kevt->keycode == KEY_DOWN) {
            terminal_scroll(term, -1);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
        if (kevt->keycode == KEY_PAGE_UP) {
            terminal_scroll(term, 10);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
        if (kevt->keycode == KEY_PAGE_DOWN) {
            terminal_scroll(term, -10);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
        if (kevt->keycode == KEY_HOME) {
            terminal_scroll(term, (int32_t)term->scrollback_count);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
        if (kevt->keycode == KEY_END) {
            terminal_scroll_to_bottom(term);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
            return;
        }
    }

    /* Enter: Execute command */
    if (kevt->keycode == KEY_ENTER) {
        if (term->scroll_offset > 0U) {
            terminal_scroll_to_bottom(term);
        }
        terminal_put_char(term, '\n');
        term->input_line[term->input_len] = '\0';

        if (term->input_len > 0) {
            shell_execute(&term->shell, term->input_line);
            if (term->shell.output_len > 0) {
                terminal_write(term, term->shell.output_buf);
            }
        }

        term->input_len = 0;
        term->input_line[0] = '\0';
        print_prompt(term);
        terminal_render(term);
        if (term->win) wm_mark_window_damaged(term->win);
        return;
    }

    /* Backspace: Erase character */
    if (kevt->keycode == KEY_BACKSPACE) {
        if (term->scroll_offset > 0U) {
            terminal_scroll_to_bottom(term);
        }
        if (term->input_len > 0) {
            term->input_len--;
            term->input_line[term->input_len] = '\0';
            terminal_put_char(term, '\b');
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
        }
        return;
    }

    /* History recall: Up Arrow */
    if (kevt->keycode == KEY_UP) {
        if (term->shell.history_count > 0) {
            if (term->shell.history_cursor == -1) {
                term->shell.history_cursor = (int32_t)term->shell.history_count - 1;
            } else if (term->shell.history_cursor > 0) {
                term->shell.history_cursor--;
            }
            while (term->input_len > 0) {
                terminal_put_char(term, '\b');
                term->input_len--;
            }
            const char *hcmd = term->shell.history[term->shell.history_cursor];
            str_copy(term->input_line, hcmd, SHELL_CMD_MAX);
            term->input_len = 0;
            while (term->input_line[term->input_len] != '\0') {
                terminal_put_char(term, term->input_line[term->input_len]);
                term->input_len++;
            }
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
        }
        return;
    }

    /* History recall: Down Arrow */
    if (kevt->keycode == KEY_DOWN) {
        if (term->shell.history_cursor >= 0) {
            term->shell.history_cursor++;
            while (term->input_len > 0) {
                terminal_put_char(term, '\b');
                term->input_len--;
            }
            if ((size_t)term->shell.history_cursor < term->shell.history_count) {
                const char *hcmd = term->shell.history[term->shell.history_cursor];
                str_copy(term->input_line, hcmd, SHELL_CMD_MAX);
                term->input_len = 0;
                while (term->input_line[term->input_len] != '\0') {
                    terminal_put_char(term, term->input_line[term->input_len]);
                    term->input_len++;
                }
            } else {
                term->shell.history_cursor = -1;
                term->input_line[0] = '\0';
                term->input_len = 0;
            }
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
        }
        return;
    }

    /* Regular ASCII character */
    if (kevt->ascii >= 32 && kevt->ascii <= 126) {
        if (term->scroll_offset > 0U) {
            terminal_scroll_to_bottom(term);
        }
        if (term->input_len + 1 < SHELL_CMD_MAX) {
            term->input_line[term->input_len++] = kevt->ascii;
            term->input_line[term->input_len] = '\0';
            terminal_put_char(term, kevt->ascii);
            terminal_render(term);
            if (term->win) wm_mark_window_damaged(term->win);
        }
    }
}

struct window *terminal_window_open(void) {
    if (!term_initialized) terminal_initialize();
    struct terminal *term = &main_terminal;

    if (term->win != NULL && term->win->active) {
        if (term->win->minimized) {
            (void)wm_minimize_window(term->win->id);
        }
        (void)wm_focus_window(term->win->id);
        (void)wm_move_window_to_workspace(term->win->id, wm_get_current_workspace());
        return term->win;
    }

    term->win = wm_create_window("Terminal", 80, 60,
                                 TERM_PIXEL_WIDTH + (2U * WM_BORDER_WIDTH),
                                 TERM_PIXEL_HEIGHT + WM_TITLEBAR_HEIGHT + (2U * WM_BORDER_WIDTH),
                                 WM_FLAG_NONE);
    if (term->win != NULL) {
        term->win->client_buffer = term->render_buffer;
        term->win->client_pitch = TERM_PIXEL_WIDTH * sizeof(uint32_t);
        term->win->client_width = TERM_PIXEL_WIDTH;
        term->win->client_height = TERM_PIXEL_HEIGHT;
        term->win->on_key = terminal_on_window_key;
        term->win->on_close = terminal_on_window_close;
        wm_mark_window_damaged(term->win);
    }
    terminal_render(term);
    return term->win;
}

bool terminal_initialize(void) {
    if (term_initialized) return true;

    struct terminal *term = &main_terminal;
    *term = (struct terminal){0};
    term->current_fg = GFX_COLOR_TEXT_PRIMARY;
    term->current_bg = GFX_COLOR_TRANSPARENT;
    term->cursor_visible = true;

    shell_initialize(&term->shell);
    terminal_clear(term);

    terminal_write(term, "\033[32m=== DAWN-OS Terminal v1.0 (Emerald Green Edition) ===\033[0m\n");
    terminal_write(term, "Type 'help' for built-in system commands.\n\n");
    print_prompt(term);

    /* Render into pixel buffer */
    terminal_render(term);

    /* Open or bind window in Window Manager */
    (void)terminal_window_open();

    term->active = true;
    term_initialized = true;
    return true;
}

struct terminal *terminal_get_instance(void) {
    if (!term_initialized) terminal_initialize();
    return &main_terminal;
}

/* Self-test */
bool terminal_self_test(void) {
    if (!terminal_initialize()) return false;
    struct terminal *term = &main_terminal;

    /* Verify grid dimensions */
    if (TERM_COLS != 80U || TERM_ROWS != 25U) return false;
    if (TERM_PIXEL_WIDTH != 640U || TERM_PIXEL_HEIGHT != 400U) return false;
    if (TERM_SCROLLBACK_ROWS != 128U) return false;

    /* Write test string */
    uint32_t prev_row = term->cursor_row;
    terminal_write(term, "SelfTest\n");
    if (term->cursor_row != prev_row + 1U) return false;

    /* Render test */
    terminal_render(term);

    /* Verify transparent background to eliminate dark box behind text */
    if (term->current_bg != GFX_COLOR_TRANSPARENT) return false;
    uint32_t last_cell_pixel = term->render_buffer[(TERM_PIXEL_HEIGHT - 1U) * TERM_PIXEL_WIDTH + (TERM_PIXEL_WIDTH - 1U)];
    if ((last_cell_pixel >> 24) != 0U) return false;

    /* Test Scrollback Ring Buffer: Write 40 lines to trigger scrolling and buffer pushes */
    uint32_t initial_sb = terminal_get_scrollback_count(term);
    for (uint32_t i = 0; i < 40U; i++) {
        terminal_write(term, "Scrollback Line Test\n");
    }
    if (terminal_get_scrollback_count(term) <= initial_sb) return false;
    if (terminal_get_scroll_offset(term) != 0U) return false;

    /* Test Scroll Viewport Operations */
    terminal_scroll(term, 5);
    if (terminal_get_scroll_offset(term) != 5U) return false;

    terminal_render(term);

    /* Simulate Shift + Up key event (scroll up 1 line) */
    struct key_event k_shift_up = {
        .keycode = KEY_UP,
        .pressed = true,
        .shift = true,
        .ctrl = false,
        .alt = false,
        .gui = false
    };
    terminal_handle_key(term, &k_shift_up);
    if (terminal_get_scroll_offset(term) != 6U) return false;

    /* Simulate Shift + Down key event (scroll down 1 line) */
    struct key_event k_shift_down = {
        .keycode = KEY_DOWN,
        .pressed = true,
        .shift = true,
        .ctrl = false,
        .alt = false,
        .gui = false
    };
    terminal_handle_key(term, &k_shift_down);
    if (terminal_get_scroll_offset(term) != 5U) return false;

    /* Scroll to bottom resets offset */
    terminal_scroll_to_bottom(term);
    if (terminal_get_scroll_offset(term) != 0U) return false;

    /* Test ANSI Clear Screen Sequence */
    terminal_write(term, "\033[2J\033[H");
    if (term->cursor_col != 0U || term->cursor_row != 0U) return false;
    print_prompt(term);

    /* Simulate typing 'echo test_d4' and pressing Enter */
    const char *test_cmd = "echo test_d4";
    for (size_t i = 0; test_cmd[i] != '\0'; i++) {
        struct key_event k_char = {
            .keycode = 0x20U,
            .ascii = test_cmd[i],
            .pressed = true,
            .shift = false,
            .ctrl = false,
            .alt = false,
            .gui = false
        };
        terminal_handle_key(term, &k_char);
    }
    struct key_event k_enter = {.keycode = KEY_ENTER, .pressed = true};
    terminal_handle_key(term, &k_enter);

    /* Test History Recall with Up Arrow */
    struct key_event k_up = {.keycode = KEY_UP, .pressed = true, .shift = false};
    terminal_handle_key(term, &k_up);
    if (term->input_len == 0) return false;

    /* Test History Dismiss with Down Arrow */
    struct key_event k_down = {.keycode = KEY_DOWN, .pressed = true, .shift = false};
    terminal_handle_key(term, &k_down);
    if (term->input_len != 0) return false;

    /* Test Long-Line Input Bounds (clamp at SHELL_CMD_MAX - 1) */
    for (size_t i = 0; i < 300U; i++) {
        struct key_event k_x = {.keycode = 0x2DU, .ascii = 'x', .pressed = true};
        terminal_handle_key(term, &k_x);
    }
    if (term->input_len != SHELL_CMD_MAX - 1U) return false;

    /* Clear line */
    while (term->input_len > 0) {
        struct key_event k_bs = {.keycode = KEY_BACKSPACE, .pressed = true};
        terminal_handle_key(term, &k_bs);
    }

    /* Test Window Manager Window Binding and Key Dispatch */
    struct window *win = terminal_window_open();
    if (win == NULL) return false;
    if (win->client_buffer != term->render_buffer) return false;
    if (win->client_width != TERM_PIXEL_WIDTH || win->client_height != TERM_PIXEL_HEIGHT) return false;
    if (win->on_key == NULL) return false;

    /* Deliver simulated key via window on_key hook */
    struct key_event k_hook_test = {.keycode = 0x1EU, .ascii = 'a', .pressed = true};
    win->on_key(win, &k_hook_test);
    if (term->input_len != 1 || term->input_line[0] != 'a') return false;

    struct key_event k_clean_bs = {.keycode = KEY_BACKSPACE, .pressed = true};
    win->on_key(win, &k_clean_bs);
    if (term->input_len != 0) return false;

    return true;
}
