/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_GFX_H
#define DAWN_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "limine_requests.h"

/* Emerald Green Neo-Liquid Glass Color Palette (ARGB8888) */
#define GFX_COLOR_BLACK             0xFF000000U
#define GFX_COLOR_WHITE             0xFFFFFFFFU
#define GFX_COLOR_TRANSPARENT       0x00000000U

/* Emerald Palette */
#define GFX_COLOR_EMERALD_DEEP      0xFF051B11U /* Deep dark background */
#define GFX_COLOR_EMERALD_BASE      0xFF0A2E1FU /* Dark emerald canvas */
#define GFX_COLOR_EMERALD_SURFACE   0xFF0E3D2AU /* Window surface */
#define GFX_COLOR_EMERALD_PRIMARY   0xFF0F5A3BU /* Primary accents */
#define GFX_COLOR_EMERALD_ACCENT    0xFF10B981U /* Bright neon emerald */
#define GFX_COLOR_EMERALD_HIGHLIGHT 0xFF34D399U /* Glass edge glow */
#define GFX_COLOR_EMERALD_CYAN      0xFF06D6A0U /* Subtle aqua tint */

/* Glass & Transparency */
#define GFX_COLOR_GLASS_PANEL       0xD00D281EU /* Translucent frosted glass */
#define GFX_COLOR_GLASS_SURFACE     0xC00A251BU /* Semi-translucent surface */
#define GFX_COLOR_GLASS_BORDER      0x6034D399U /* Border specular reflection */
#define GFX_COLOR_GLASS_SHADOW      0x80000000U /* Soft ambient shadow */

/* Typography */
#define GFX_COLOR_TEXT_PRIMARY      0xFFF0FDF4U /* Crisp off-white */
#define GFX_COLOR_TEXT_SECONDARY    0xFFA7F3D0U /* Light sage */
#define GFX_COLOR_TEXT_MUTED        0xFF6EE7B7U /* Dimmed emerald text */

/* Window Controls */
#define GFX_COLOR_BTN_CLOSE         0xFFEF4444U /* Ruby red */
#define GFX_COLOR_BTN_MINIMIZE      0xFFF59E0BU /* Amber */
#define GFX_COLOR_BTN_MAXIMIZE      0xFF10B981U /* Emerald */

/* Compatibility Color Aliases */
#define GFX_COLOR_WINDOW_BG         GFX_COLOR_EMERALD_SURFACE
#define GFX_COLOR_PANEL_DARK        GFX_COLOR_EMERALD_BASE
#define GFX_COLOR_EMERALD_MUTED     GFX_COLOR_TEXT_MUTED
#define GFX_COLOR_EMERALD_NEON      GFX_COLOR_EMERALD_ACCENT

struct gfx_surface {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch; /* in bytes */
};

typedef struct gfx_surface gfx_surface_t;

bool gfx_initialize(void);
struct gfx_surface *gfx_get_framebuffer(void);
struct gfx_surface *gfx_get_backbuffer(void);
void gfx_flip(void);

/* Primitives */
void gfx_put_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color);
void gfx_blend_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color);
void gfx_draw_rect_filled(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_rect_outline(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

static inline void gfx_fill_rect(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    gfx_draw_rect_filled(surf, x, y, w, h, color);
}

static inline void gfx_draw_rect(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    gfx_draw_rect_outline(surf, x, y, w, h, color);
}
void gfx_draw_line(struct gfx_surface *surf, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void gfx_draw_gradient_v(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t top_col, uint32_t bot_col);
void gfx_draw_glass_panel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t base_color);

/* Typography & Font */
void gfx_draw_char(struct gfx_surface *surf, uint32_t x, uint32_t y, char c, uint32_t color);
void gfx_draw_string(struct gfx_surface *surf, uint32_t x, uint32_t y, const char *str, uint32_t color);

/* Icons */
enum gfx_icon {
    GFX_ICON_TERMINAL = 0,
    GFX_ICON_FILE_EXPLORER = 1,
    GFX_ICON_TASK_MANAGER = 2,
    GFX_ICON_SETTINGS = 3,
    GFX_ICON_COMPUTER = 4,
    GFX_ICON_SHUTDOWN = 5,
    GFX_ICON_DAWN_LOGO = 6
};

void gfx_draw_icon(struct gfx_surface *surf, uint32_t x, uint32_t y, enum gfx_icon icon, uint32_t color);

bool gfx_self_test(void);

#endif /* DAWN_GFX_H */
