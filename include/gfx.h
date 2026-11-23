/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_GFX_H
#define DAWN_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "limine_requests.h"

/* ========================================================================= */
/* Phase D0: Emerald Green Neo-Liquid Glass Design System Tokens             */
/* ========================================================================= */

/* Surface & Canvas Palette (ARGB8888) */
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

/* State & Accessibility Tokens (D0) */
#define GFX_COLOR_FOCUS_RING        0xFF38BDF8U /* Neon sky/cyan focus indicator */
#define GFX_COLOR_DISABLED          0x80374151U /* Muted disabled state */
#define GFX_COLOR_WARNING           0xFFF59E0BU /* Amber warning */
#define GFX_COLOR_ERROR             0xFFEF4444U /* Ruby red error */
#define GFX_COLOR_SUCCESS           0xFF10B981U /* Emerald success */

/* Window Controls */
#define GFX_COLOR_BTN_CLOSE         0xFFEF4444U /* Ruby red */
#define GFX_COLOR_BTN_MINIMIZE      0xFFF59E0BU /* Amber */
#define GFX_COLOR_BTN_MAXIMIZE      0xFF10B981U /* Emerald */

/* Compatibility Color Aliases */
#define GFX_COLOR_WINDOW_BG         GFX_COLOR_EMERALD_SURFACE
#define GFX_COLOR_PANEL_DARK        GFX_COLOR_EMERALD_BASE
#define GFX_COLOR_EMERALD_MUTED     GFX_COLOR_TEXT_MUTED
#define GFX_COLOR_EMERALD_NEON      GFX_COLOR_EMERALD_ACCENT

/* Spatial Tokens (4px / 8px Grid) */
#define GFX_SPACE_XS                2U
#define GFX_SPACE_SM                4U
#define GFX_SPACE_MD                8U
#define GFX_SPACE_LG                16U
#define GFX_SPACE_XL                24U
#define GFX_SPACE_2XL               32U

/* Radii Tokens */
#define GFX_RADIUS_SM               4U
#define GFX_RADIUS_MD               8U
#define GFX_RADIUS_LG               12U

/* Desktop Layout & Chrome Constants */
#define DESKTOP_STATUSBAR_HEIGHT    28U
#define DESKTOP_DOCK_HEIGHT         48U
#define DESKTOP_MIN_WIDTH           640U
#define DESKTOP_MIN_HEIGHT          480U
#define DESKTOP_MAX_WIDTH           7680U
#define DESKTOP_MAX_HEIGHT          4320U

/* Pixel Formats */
enum gfx_pixel_format {
    GFX_FORMAT_ARGB8888 = 0,
    GFX_FORMAT_ABGR8888 = 1,
    GFX_FORMAT_UNKNOWN  = 2
};

struct gfx_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct gfx_surface {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     /* in bytes */
    size_t buffer_size; /* in bytes */
    bool is_hardware_fb;
    uint8_t format;     /* enum gfx_pixel_format */
};

typedef struct gfx_surface gfx_surface_t;

/* Initialization and Surface Management */
bool gfx_initialize(void);
bool gfx_validate_framebuffer(const struct limine_framebuffer *lfb);
bool gfx_surface_init(struct gfx_surface *surf, uint32_t *buffer,
                      uint32_t width, uint32_t height, uint32_t pitch,
                      size_t buffer_size, bool is_hardware_fb, uint8_t format);
bool gfx_surface_is_valid(const struct gfx_surface *surf);

struct gfx_surface *gfx_get_framebuffer(void);
struct gfx_surface *gfx_get_backbuffer(void);
void gfx_flip(void);
static inline void gfx_swap_buffers(void) { gfx_flip(); }
void gfx_clear(struct gfx_surface *surf, uint32_t color);

/* Primitives */
void gfx_put_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color);
void gfx_blend_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color);
void gfx_draw_rect_filled(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_rect_filled_signed(struct gfx_surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
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

/* Clipped Blit Primitives */
bool gfx_blit(struct gfx_surface *dst, int32_t dst_x, int32_t dst_y,
              const struct gfx_surface *src, int32_t src_x, int32_t src_y,
              uint32_t width, uint32_t height);
bool gfx_blit_alpha(struct gfx_surface *dst, int32_t dst_x, int32_t dst_y,
                    const struct gfx_surface *src, int32_t src_x, int32_t src_y,
                    uint32_t width, uint32_t height);

/* Dirty Region Damage Tracking & Scheduled Frame Flushing */
void gfx_damage_add(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void gfx_damage_add_fullscreen(void);
bool gfx_damage_is_dirty(void);
struct gfx_rect gfx_damage_get_bounds(void);
void gfx_damage_clear(void);
void gfx_flush(void);

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
    GFX_ICON_DAWN_LOGO = 6,
    GFX_ICON_CALCULATOR = 7,
    GFX_ICON_CALENDAR = 8
};

void gfx_draw_icon(struct gfx_surface *surf, uint32_t x, uint32_t y, enum gfx_icon icon, uint32_t color);

bool gfx_self_test(void);

#endif /* DAWN_GFX_H */
