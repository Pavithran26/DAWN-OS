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

static void print_prompt(struct terminal *term) {
    terminal_write(term, "\033[32madmin@dawn-os\033[0m:");
    terminal_write(term, term->shell.cwd);
    terminal_write(term, "$ ");
}

static void scroll(struct terminal *term) {
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

void terminal_clear(struct terminal *term) {
    if (term == NULL) return;
    for (uint32_t r = 0; r < TERM_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_COLS; c++) {
            term->grid[r][c] = (struct term_cell){' ', term->current_fg, term->current_bg};
        }
    }
    term->cursor_col = 0;
    term->cursor_row = 0;
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
        /* Check ANSI escape sequence \033[... */
        if (str[i] == '\033' && str[i + 1] == '[') {
            i += 2;
            if (str[i] == '3' && str[i + 1] == '2' && str[i + 2] == 'm') {
                term->current_fg = GFX_COLOR_EMERALD_ACCENT;
                i += 2;
                continue;
            } else if (str[i] == '0' && str[i + 1] == 'm') {
                term->current_fg = GFX_COLOR_TEXT_PRIMARY;
                i += 1;
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
            struct term_cell cell = term->grid[r][c];
            uint32_t px = c * 8U;
            uint32_t py = r * 16U;

            /* Draw background block only if cell background is not transparent */
            if ((cell.bg >> 24) != 0U) {
                gfx_draw_rect_filled(&surf, px, py, 8, 16, cell.bg);
            }

            /* Draw glyph */
            if (cell.ch != ' ') {
                gfx_draw_char(&surf, px, py, cell.ch, cell.fg);
            }
        }
    }

    /* Draw blinking cursor */
    if (term->cursor_visible && term->cursor_col < TERM_COLS && term->cursor_row < TERM_ROWS) {
        uint32_t cx = term->cursor_col * 8U;
        uint32_t cy = term->cursor_row * 16U;
        gfx_draw_rect_filled(&surf, cx, cy + 13, 8, 3, GFX_COLOR_EMERALD_ACCENT);
    }
}

void terminal_handle_key(struct terminal *term, const struct key_event *kevt) {
    if (term == NULL || kevt == NULL || !kevt->pressed) return;

    if (kevt->keycode == KEY_ENTER) {
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
        return;
    }

    if (kevt->keycode == KEY_BACKSPACE) {
        if (term->input_len > 0) {
            term->input_len--;
            term->input_line[term->input_len] = '\0';
            terminal_put_char(term, '\b');
            terminal_render(term);
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
            /* Erase current line */
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
        }
        return;
    }

    /* Regular character */
    if (kevt->ascii >= 32 && kevt->ascii <= 126) {
        if (term->input_len + 1 < SHELL_CMD_MAX) {
            term->input_line[term->input_len++] = kevt->ascii;
            term->input_line[term->input_len] = '\0';
            terminal_put_char(term, kevt->ascii);
            terminal_render(term);
        }
    }
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

    /* Hook into Window Manager */
    term->win = wm_create_window("DAWN Terminal", 80, 60, TERM_PIXEL_WIDTH + 4, TERM_PIXEL_HEIGHT + WM_TITLEBAR_HEIGHT + 4, WM_FLAG_NONE);
    if (term->win != NULL) {
        term->win->client_buffer = term->render_buffer;
        term->win->client_pitch = TERM_PIXEL_WIDTH * sizeof(uint32_t);
    }

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
    if (TERM_COLS != 80 || TERM_ROWS != 25) return false;
    if (TERM_PIXEL_WIDTH != 640 || TERM_PIXEL_HEIGHT != 400) return false;

    /* Write test string */
    uint32_t prev_row = term->cursor_row;
    terminal_write(term, "SelfTest\n");
    if (term->cursor_row != prev_row + 1) return false;

    /* Render test */
    terminal_render(term);

    /* Verify transparent background to eliminate dark box behind text */
    if (term->current_bg != GFX_COLOR_TRANSPARENT) return false;
    uint32_t last_cell_pixel = term->render_buffer[(TERM_PIXEL_HEIGHT - 1U) * TERM_PIXEL_WIDTH + (TERM_PIXEL_WIDTH - 1U)];
    if ((last_cell_pixel >> 24) != 0U) return false;

    /* Simulate Enter key to execute echo */
    struct key_event k_e = {.keycode = 0x12, .ascii = 'e', .pressed = true};
    struct key_event k_c = {.keycode = 0x2E, .ascii = 'c', .pressed = true};
    struct key_event k_h = {.keycode = 0x23, .ascii = 'h', .pressed = true};
    struct key_event k_o = {.keycode = 0x18, .ascii = 'o', .pressed = true};
    struct key_event k_ret = {.keycode = KEY_ENTER, .pressed = true};

    terminal_handle_key(term, &k_e);
    terminal_handle_key(term, &k_c);
    terminal_handle_key(term, &k_h);
    terminal_handle_key(term, &k_o);
    terminal_handle_key(term, &k_ret);

    return true;
}
