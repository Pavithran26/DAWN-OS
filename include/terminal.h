/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_TERMINAL_H
#define DAWN_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "window_manager.h"
#include "shell.h"
#include "input.h"

#define TERM_COLS 80U
#define TERM_ROWS 25U
#define TERM_PIXEL_WIDTH  (TERM_COLS * 8U)
#define TERM_PIXEL_HEIGHT (TERM_ROWS * 16U)

struct term_cell {
    char ch;
    uint32_t fg;
    uint32_t bg;
};

struct terminal {
    struct term_cell grid[TERM_ROWS][TERM_COLS];
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t current_fg;
    uint32_t current_bg;
    bool cursor_visible;
    char input_line[SHELL_CMD_MAX];
    size_t input_len;
    struct shell_context shell;
    struct window *win;
    uint32_t render_buffer[TERM_PIXEL_WIDTH * TERM_PIXEL_HEIGHT];
    bool active;
};

bool terminal_initialize(void);
struct terminal *terminal_get_instance(void);
void terminal_write(struct terminal *term, const char *str);
void terminal_put_char(struct terminal *term, char c);
void terminal_clear(struct terminal *term);
void terminal_handle_key(struct terminal *term, const struct key_event *kevt);
void terminal_render(struct terminal *term);

bool terminal_self_test(void);

#endif /* DAWN_TERMINAL_H */
