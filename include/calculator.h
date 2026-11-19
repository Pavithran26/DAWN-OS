/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_CALCULATOR_H
#define DAWN_CALCULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALCULATOR_WIDTH  260U
#define CALCULATOR_HEIGHT 340U

bool calculator_initialize(void);
bool calculator_window_open(void);
void calculator_window_close(void);
void calculator_window_handle_click(int32_t local_x, int32_t local_y);
void calculator_window_render(void);
bool calculator_self_test(void);

#endif /* DAWN_CALCULATOR_H */
