/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_CALENDAR_H
#define DAWN_CALENDAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALENDAR_WIDTH  300U
#define CALENDAR_HEIGHT 280U

bool calendar_initialize(void);
bool calendar_window_open(void);
void calendar_window_close(void);
void calendar_window_handle_click(int32_t local_x, int32_t local_y);
void calendar_window_render(void);
bool calendar_self_test(void);

#endif /* DAWN_CALENDAR_H */
