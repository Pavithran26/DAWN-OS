/* SPDX-License-Identifier: Unlicense */
#include "gfx.h"

#define DEFAULT_FB_WIDTH  1280U
#define DEFAULT_FB_HEIGHT 800U

static uint32_t backbuffer_storage[DEFAULT_FB_WIDTH * DEFAULT_FB_HEIGHT];
static struct gfx_surface fb_surface = {0};
static struct gfx_surface backbuffer_surface = {0};
static bool gfx_initialized = false;
static bool gfx_hardware_fb_valid = false;

/* Bounded damage tracking state */
struct gfx_damage {
    bool dirty;
    uint32_t min_x;
    uint32_t min_y;
    uint32_t max_x;
    uint32_t max_y;
};

static struct gfx_damage g_damage = {
    .dirty = false,
    .min_x = 0U,
    .min_y = 0U,
    .max_x = 0U,
    .max_y = 0U
};

/* 8x16 Monospace Font (Standard ASCII 32..126) */
static const uint8_t font_8x16[95][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, /* 33 '!' */
    {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 '"' */
    {0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, /* 35 '#' */
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00}, /* 36 '$' */
    {0x00,0x00,0x00,0xC6,0xCC,0x18,0x30,0x60,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 37 '%' */
    {0x00,0x38,0x6C,0x68,0x30,0x7A,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00}, /* 38 '&' */
    {0x00,0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 39 '\'' */
    {0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* 40 '(' */
    {0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* 41 ')' */
    {0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 42 '*' */
    {0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 43 '+' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x08,0x10,0x00,0x00,0x00}, /* 44 ',' */
    {0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 45 '-' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 46 '.' */
    {0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00}, /* 47 '/' */
    {0x00,0x3C,0x66,0xC3,0xC3,0xDB,0xDB,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 48 '0' */
    {0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 49 '1' */
    {0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 50 '2' */
    {0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 51 '3' */
    {0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00}, /* 52 '4' */
    {0x00,0xFE,0xC0,0xC0,0xFC,0x06,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 53 '5' */
    {0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 54 '6' */
    {0x00,0xFE,0xC6,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00}, /* 55 '7' */
    {0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 56 '8' */
    {0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00}, /* 57 '9' */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 58 ':' */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x08,0x10,0x00,0x00,0x00,0x00,0x00}, /* 59 ';' */
    {0x00,0x0E,0x1C,0x38,0x70,0xE0,0x70,0x38,0x1C,0x0E,0x00,0x00,0x00,0x00,0x00,0x00}, /* 60 '<' */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 61 '=' */
    {0x00,0x70,0x38,0x1C,0x0E,0x07,0x0E,0x1C,0x38,0x70,0x00,0x00,0x00,0x00,0x00,0x00}, /* 62 '>' */
    {0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 63 '?' */
    {0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 64 '@' */
    {0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 65 'A' */
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00,0x00}, /* 66 'B' */
    {0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 67 'C' */
    {0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00,0x00}, /* 68 'D' */
    {0x00,0xFE,0x62,0x60,0x64,0x7C,0x64,0x60,0x62,0x62,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 69 'E' */
    {0x00,0xFE,0x66,0x62,0x64,0x7C,0x64,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00}, /* 70 'F' */
    {0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00,0x00}, /* 71 'G' */
    {0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 72 'H' */
    {0x00,0x78,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,0x00}, /* 73 'I' */
    {0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,0x00}, /* 74 'J' */
    {0x00,0xE6,0x66,0x6C,0x68,0x78,0x68,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00}, /* 75 'K' */
    {0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x62,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 76 'L' */
    {0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 77 'M' */
    {0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 78 'N' */
    {0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 79 'O' */
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00}, /* 80 'P' */
    {0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E,0x00,0x00,0x00,0x00,0x00}, /* 81 'Q' */
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00}, /* 82 'R' */
    {0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 83 'S' */
    {0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 84 'T' */
    {0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 85 'U' */
    {0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00}, /* 86 'V' */
    {0x00,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0x6C,0x6C,0x28,0x00,0x00,0x00,0x00,0x00}, /* 87 'W' */
    {0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 88 'X' */
    {0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 89 'Y' */
    {0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 90 'Z' */
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 91 '[' */
    {0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00,0x00}, /* 92 '\\' */
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 93 ']' */
    {0x00,0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 94 '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00}, /* 95 '_' */
    {0x00,0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 96 '`' */
    {0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00}, /* 97 'a' */
    {0x00,0xE0,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 98 'b' */
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 99 'c' */
    {0x00,0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00}, /* 100 'd' */
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 101 'e' */
    {0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00}, /* 102 'f' */
    {0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0x7C,0x0C,0x78,0x00,0x00,0x00,0x00,0x00}, /* 103 'g' */
    {0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00}, /* 104 'h' */
    {0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00}, /* 105 'i' */
    {0x00,0x0C,0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, /* 106 'j' */
    {0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00,0x00}, /* 107 'k' */
    {0x00,0x70,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,0x00}, /* 108 'l' */
    {0x00,0x00,0x00,0x00,0xFE,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0x00,0x00,0x00,0x00,0x00}, /* 109 'm' */
    {0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00}, /* 110 'n' */
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 111 'o' */
    {0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0xF0,0x00,0x00,0x00,0x00}, /* 112 'p' */
    {0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0x7C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00}, /* 113 'q' */
    {0x00,0x00,0x00,0x00,0xDC,0x76,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00}, /* 114 'r' */
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00}, /* 115 's' */
    {0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x34,0x18,0x00,0x00,0x00,0x00,0x00}, /* 116 't' */
    {0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00}, /* 117 'u' */
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00}, /* 118 'v' */
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x28,0x00,0x00,0x00,0x00,0x00}, /* 119 'w' */
    {0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00}, /* 120 'x' */
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00,0x00,0x00}, /* 121 'y' */
    {0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 122 'z' */
    {0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00}, /* 123 '{' */
    {0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, /* 124 '|' */
    {0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,0x00}, /* 125 '}' */
    {0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  /* 126 '~' */
};

/* Validate framebuffer metadata strictly according to Phase D1 requirements */
bool gfx_validate_framebuffer(const struct limine_framebuffer *lfb) {
    if (lfb == NULL) return false;
    if (lfb->address == NULL) return false;

    /* Framebuffer virtual address must reside in higher-half canonical supervisor address space */
    if ((uintptr_t)lfb->address < 0xFFFF800000000000ULL) return false;

    /* Non-zero and reasonable display bounds */
    if (lfb->width == 0ULL || lfb->height == 0ULL) return false;
    if (lfb->width > (uint64_t)DESKTOP_MAX_WIDTH || lfb->height > (uint64_t)DESKTOP_MAX_HEIGHT) return false;

    /* Require 32 bits per pixel linear framebuffer */
    if (lfb->bpp != 32U) return false;

    /* Pitch must be at least width * 4 bytes and 4-byte aligned */
    if (lfb->pitch < lfb->width * 4ULL) return false;
    if ((lfb->pitch & 3ULL) != 0ULL) return false;

    /* Arithmetic overflow check for pitch * height */
    if (lfb->pitch > UINT64_MAX / lfb->height) return false;

    /* Total size must not exceed safety limit (256 MiB) */
    uint64_t total_size = lfb->pitch * lfb->height;
    if (total_size > 256ULL * 1024ULL * 1024ULL) return false;

    /* Require 8-bit standard color channel sizes */
    if (lfb->red_mask_size != 8U || lfb->green_mask_size != 8U || lfb->blue_mask_size != 8U) {
        return false;
    }

    return true;
}

bool gfx_surface_init(struct gfx_surface *surf, uint32_t *buffer,
                      uint32_t width, uint32_t height, uint32_t pitch,
                      size_t buffer_size, bool is_hardware_fb, uint8_t format) {
    if (surf == NULL || buffer == NULL) return false;
    if (width == 0U || height == 0U) return false;
    if (pitch < width * sizeof(uint32_t) || (pitch & 3U) != 0U) return false;

    /* Size bounds verification */
    size_t min_needed = (size_t)pitch * (size_t)height;
    if (buffer_size < min_needed) return false;

    surf->buffer = buffer;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->buffer_size = buffer_size;
    surf->is_hardware_fb = is_hardware_fb;
    surf->format = format;

    return true;
}

bool gfx_surface_is_valid(const struct gfx_surface *surf) {
    if (surf == NULL || surf->buffer == NULL) return false;
    if (surf->width == 0U || surf->height == 0U) return false;
    if (surf->pitch < surf->width * sizeof(uint32_t) || (surf->pitch & 3U) != 0U) return false;
    if (surf->buffer_size > 0U) {
        size_t min_needed = (size_t)surf->pitch * (size_t)surf->height;
        if (surf->buffer_size < min_needed) return false;
    }
    return true;
}

bool gfx_initialize(void) {
    if (gfx_initialized) return true;

    /* 1. Initialize memory-backed backbuffer */
    (void)gfx_surface_init(&backbuffer_surface, backbuffer_storage,
                           DEFAULT_FB_WIDTH, DEFAULT_FB_HEIGHT,
                           DEFAULT_FB_WIDTH * sizeof(uint32_t),
                           sizeof(backbuffer_storage), false,
                           (uint8_t)GFX_FORMAT_ARGB8888);

    /* 2. Validate and initialize hardware framebuffer if provided by bootloader */
    gfx_hardware_fb_valid = false;
    if (framebuffer_response_valid()) {
        const struct limine_framebuffer *lfb = limine_framebuffer_request.response->framebuffers[0];
        if (gfx_validate_framebuffer(lfb)) {
            uint8_t fmt = (lfb->blue_mask_shift == 16U && lfb->red_mask_shift == 0U)
                          ? (uint8_t)GFX_FORMAT_ABGR8888
                          : (uint8_t)GFX_FORMAT_ARGB8888;

            size_t fb_size = (size_t)(lfb->pitch * lfb->height);
            if (gfx_surface_init(&fb_surface, (uint32_t *)lfb->address,
                                 (uint32_t)lfb->width, (uint32_t)lfb->height,
                                 (uint32_t)lfb->pitch, fb_size, true, fmt)) {
                gfx_hardware_fb_valid = true;

                /* Constrain backbuffer to match display dimensions if smaller than maximum storage */
                if (fb_surface.width <= DEFAULT_FB_WIDTH && fb_surface.height <= DEFAULT_FB_HEIGHT) {
                    backbuffer_surface.width = fb_surface.width;
                    backbuffer_surface.height = fb_surface.height;
                    backbuffer_surface.pitch = fb_surface.width * sizeof(uint32_t);
                }
            }
        }
    }

    if (!gfx_hardware_fb_valid) {
        fb_surface = backbuffer_surface;
    }

    gfx_damage_clear();
    gfx_initialized = true;
    return true;
}

struct gfx_surface *gfx_get_framebuffer(void) {
    if (!gfx_initialized) (void)gfx_initialize();
    return &fb_surface;
}

struct gfx_surface *gfx_get_backbuffer(void) {
    if (!gfx_initialized) (void)gfx_initialize();
    return &backbuffer_surface;
}

void gfx_clear(struct gfx_surface *surf, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;

    uint32_t out_color = color;
    if (surf->format == (uint8_t)GFX_FORMAT_ABGR8888) {
        uint32_t r = (color >> 16) & 0xFFU;
        uint32_t b = color & 0xFFU;
        out_color = (color & 0xFF00FF00U) | (b << 16) | r;
    }

    uint32_t stride = surf->pitch / 4U;
    for (uint32_t y = 0; y < surf->height; y++) {
        size_t line_offset = (size_t)y * stride;
        if (surf->buffer_size > 0U && (line_offset + surf->width) * sizeof(uint32_t) > surf->buffer_size) break;
        uint32_t *line = &surf->buffer[line_offset];
        for (uint32_t x = 0; x < surf->width; x++) {
            line[x] = out_color;
        }
    }
}

void gfx_put_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;
    if (x >= surf->width || y >= surf->height) return;

    uint32_t stride = surf->pitch / 4U;
    size_t offset = (size_t)y * stride + x;
    if (surf->buffer_size > 0U && (offset + 1U) * sizeof(uint32_t) > surf->buffer_size) return;

    if (surf->format == (uint8_t)GFX_FORMAT_ABGR8888) {
        uint32_t r = (color >> 16) & 0xFFU;
        uint32_t b = color & 0xFFU;
        color = (color & 0xFF00FF00U) | (b << 16) | r;
    }

    surf->buffer[offset] = color;
}

void gfx_blend_pixel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;
    if (x >= surf->width || y >= surf->height) return;

    uint32_t a = (color >> 24) & 0xFFU;
    if (a == 255U) {
        gfx_put_pixel(surf, x, y, color);
    } else if (a > 0U) {
        uint32_t stride = surf->pitch / 4U;
        size_t offset = (size_t)y * stride + x;
        if (surf->buffer_size > 0U && (offset + 1U) * sizeof(uint32_t) > surf->buffer_size) return;

        uint32_t bg = surf->buffer[offset];
        if (surf->format == (uint8_t)GFX_FORMAT_ABGR8888) {
            uint32_t r = (bg >> 16) & 0xFFU;
            uint32_t b = bg & 0xFFU;
            bg = (bg & 0xFF00FF00U) | (b << 16) | r;
        }

        uint32_t inv_a = 255U - a;
        uint32_t rb = (((color & 0x00FF00FFU) * a) + ((bg & 0x00FF00FFU) * inv_a)) >> 8;
        uint32_t g  = (((color & 0x0000FF00U) * a) + ((bg & 0x0000FF00U) * inv_a)) >> 8;
        uint32_t result = 0xFF000000U | (rb & 0x00FF00FFU) | (g & 0x0000FF00U);

        if (surf->format == (uint8_t)GFX_FORMAT_ABGR8888) {
            uint32_t res_r = (result >> 16) & 0xFFU;
            uint32_t res_b = result & 0xFFU;
            result = (result & 0xFF00FF00U) | (res_b << 16) | res_r;
        }

        surf->buffer[offset] = result;
    }
}

void gfx_draw_rect_filled(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;
    if (x >= surf->width || y >= surf->height || w == 0U || h == 0U) return;

    /* Bounded subtraction to prevent uint32_t wrap-around */
    if (w > surf->width - x) w = surf->width - x;
    if (h > surf->height - y) h = surf->height - y;

    uint32_t a = (color >> 24) & 0xFFU;
    uint32_t stride = surf->pitch / 4U;

    if (a == 255U) {
        uint32_t out_color = color;
        if (surf->format == (uint8_t)GFX_FORMAT_ABGR8888) {
            uint32_t r = (color >> 16) & 0xFFU;
            uint32_t b = color & 0xFFU;
            out_color = (color & 0xFF00FF00U) | (b << 16) | r;
        }
        for (uint32_t r = 0; r < h; r++) {
            size_t row_offset = (size_t)(y + r) * stride + x;
            if (surf->buffer_size > 0U && (row_offset + w) * sizeof(uint32_t) > surf->buffer_size) break;
            uint32_t *line = &surf->buffer[row_offset];
            for (uint32_t c = 0; c < w; c++) {
                line[c] = out_color;
            }
        }
    } else if (a > 0U) {
        for (uint32_t r = 0; r < h; r++) {
            for (uint32_t c = 0; c < w; c++) {
                gfx_blend_pixel(surf, x + c, y + r, color);
            }
        }
    }
}

void gfx_draw_rect_filled_signed(struct gfx_surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (surf == NULL || w <= 0 || h <= 0) return;

    /* Clip left edge */
    if (x < 0) {
        int32_t clip = -x;
        if (clip >= w) return;
        w -= clip;
        x = 0;
    }
    /* Clip top edge */
    if (y < 0) {
        int32_t clip = -y;
        if (clip >= h) return;
        h -= clip;
        y = 0;
    }

    gfx_draw_rect_filled(surf, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

void gfx_draw_rect_outline(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;
    if (x >= surf->width || y >= surf->height || w == 0U || h == 0U) return;

    if (w > surf->width - x) w = surf->width - x;
    if (h > surf->height - y) h = surf->height - y;

    for (uint32_t c = 0; c < w; c++) {
        gfx_blend_pixel(surf, x + c, y, color);
        if (h > 1U) gfx_blend_pixel(surf, x + c, y + h - 1U, color);
    }
    for (uint32_t r = 0; r < h; r++) {
        gfx_blend_pixel(surf, x, y + r, color);
        if (w > 1U) gfx_blend_pixel(surf, x + w - 1U, y + r, color);
    }
}

void gfx_draw_line(struct gfx_surface *surf, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;

    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    while (true) {
        if (x0 >= 0 && (uint32_t)x0 < surf->width && y0 >= 0 && (uint32_t)y0 < surf->height) {
            gfx_blend_pixel(surf, (uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_draw_gradient_v(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t top_col, uint32_t bot_col) {
    if (!gfx_surface_is_valid(surf) || h == 0U) return;

    uint32_t r0 = (top_col >> 16) & 0xFFU, g0 = (top_col >> 8) & 0xFFU, b0 = top_col & 0xFFU;
    uint32_t r1 = (bot_col >> 16) & 0xFFU, g1 = (bot_col >> 8) & 0xFFU, b1 = bot_col & 0xFFU;

    int32_t dr = (int32_t)r1 - (int32_t)r0;
    int32_t dg = (int32_t)g1 - (int32_t)g0;
    int32_t db = (int32_t)b1 - (int32_t)b0;

    for (uint32_t r = 0; r < h; r++) {
        uint32_t cr = (uint32_t)((int32_t)r0 + (dr * (int32_t)r) / (int32_t)h) & 0xFFU;
        uint32_t cg = (uint32_t)((int32_t)g0 + (dg * (int32_t)r) / (int32_t)h) & 0xFFU;
        uint32_t cb = (uint32_t)((int32_t)b0 + (db * (int32_t)r) / (int32_t)h) & 0xFFU;
        uint32_t col = 0xFF000000U | (cr << 16) | (cg << 8) | cb;
        gfx_draw_rect_filled(surf, x, y + r, w, 1U, col);
    }
}

void gfx_draw_glass_panel(struct gfx_surface *surf, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t base_color) {
    if (w < 4U || h < 4U) return;
    /* Soft ambient drop shadow */
    gfx_draw_rect_filled(surf, x + 2U, y + 2U, w, h, 0x30000000U);
    /* Frosted glass body */
    gfx_draw_rect_filled(surf, x, y, w, h, base_color);
    /* Top specular highlight line */
    gfx_draw_line(surf, (int32_t)(x + 1U), (int32_t)y, (int32_t)(x + w - 2U), (int32_t)y, GFX_COLOR_GLASS_BORDER);
    /* Frosted border */
    gfx_draw_rect_outline(surf, x, y, w, h, GFX_COLOR_GLASS_BORDER);
}

/* Clipped Blit Primitives */
bool gfx_blit(struct gfx_surface *dst, int32_t dst_x, int32_t dst_y,
              const struct gfx_surface *src, int32_t src_x, int32_t src_y,
              uint32_t width, uint32_t height) {
    if (!gfx_surface_is_valid(dst) || !gfx_surface_is_valid(src)) return false;
    if (width == 0U || height == 0U) return true;

    /* Source clipping */
    if (src_x < 0) {
        int32_t clip = -src_x;
        if ((uint32_t)clip >= width) return true;
        width -= (uint32_t)clip;
        dst_x += clip;
        src_x = 0;
    }
    if (src_y < 0) {
        int32_t clip = -src_y;
        if ((uint32_t)clip >= height) return true;
        height -= (uint32_t)clip;
        dst_y += clip;
        src_y = 0;
    }
    if ((uint32_t)src_x >= src->width || (uint32_t)src_y >= src->height) return true;
    if (width > src->width - (uint32_t)src_x) width = src->width - (uint32_t)src_x;
    if (height > src->height - (uint32_t)src_y) height = src->height - (uint32_t)src_y;

    /* Destination clipping */
    if (dst_x < 0) {
        int32_t clip = -dst_x;
        if ((uint32_t)clip >= width) return true;
        width -= (uint32_t)clip;
        src_x += clip;
        dst_x = 0;
    }
    if (dst_y < 0) {
        int32_t clip = -dst_y;
        if ((uint32_t)clip >= height) return true;
        height -= (uint32_t)clip;
        dst_y += clip;
        src_y = 0;
    }
    if ((uint32_t)dst_x >= dst->width || (uint32_t)dst_y >= dst->height) return true;
    if (width > dst->width - (uint32_t)dst_x) width = dst->width - (uint32_t)dst_x;
    if (height > dst->height - (uint32_t)dst_y) height = dst->height - (uint32_t)dst_y;

    if (width == 0U || height == 0U) return true;

    uint32_t src_stride = src->pitch / 4U;
    uint32_t dst_stride = dst->pitch / 4U;
    bool reformat = (dst->format != src->format);

    for (uint32_t r = 0; r < height; r++) {
        size_t src_off = (size_t)((uint32_t)src_y + r) * src_stride + (uint32_t)src_x;
        size_t dst_off = (size_t)((uint32_t)dst_y + r) * dst_stride + (uint32_t)dst_x;
        if (src->buffer_size > 0U && (src_off + width) * sizeof(uint32_t) > src->buffer_size) break;
        if (dst->buffer_size > 0U && (dst_off + width) * sizeof(uint32_t) > dst->buffer_size) break;

        const uint32_t *src_line = &src->buffer[src_off];
        uint32_t *dst_line = &dst->buffer[dst_off];

        if (!reformat) {
            for (uint32_t c = 0; c < width; c++) {
                dst_line[c] = src_line[c];
            }
        } else {
            for (uint32_t c = 0; c < width; c++) {
                uint32_t col = src_line[c];
                uint32_t red = (col >> 16) & 0xFFU;
                uint32_t blue = col & 0xFFU;
                dst_line[c] = (col & 0xFF00FF00U) | (blue << 16) | red;
            }
        }
    }
    return true;
}

bool gfx_blit_alpha(struct gfx_surface *dst, int32_t dst_x, int32_t dst_y,
                    const struct gfx_surface *src, int32_t src_x, int32_t src_y,
                    uint32_t width, uint32_t height) {
    if (!gfx_surface_is_valid(dst) || !gfx_surface_is_valid(src)) return false;
    if (width == 0U || height == 0U) return true;

    /* Source clipping */
    if (src_x < 0) {
        int32_t clip = -src_x;
        if ((uint32_t)clip >= width) return true;
        width -= (uint32_t)clip;
        dst_x += clip;
        src_x = 0;
    }
    if (src_y < 0) {
        int32_t clip = -src_y;
        if ((uint32_t)clip >= height) return true;
        height -= (uint32_t)clip;
        dst_y += clip;
        src_y = 0;
    }
    if ((uint32_t)src_x >= src->width || (uint32_t)src_y >= src->height) return true;
    if (width > src->width - (uint32_t)src_x) width = src->width - (uint32_t)src_x;
    if (height > src->height - (uint32_t)src_y) height = src->height - (uint32_t)src_y;

    /* Destination clipping */
    if (dst_x < 0) {
        int32_t clip = -dst_x;
        if ((uint32_t)clip >= width) return true;
        width -= (uint32_t)clip;
        src_x += clip;
        dst_x = 0;
    }
    if (dst_y < 0) {
        int32_t clip = -dst_y;
        if ((uint32_t)clip >= height) return true;
        height -= (uint32_t)clip;
        dst_y += clip;
        src_y = 0;
    }
    if ((uint32_t)dst_x >= dst->width || (uint32_t)dst_y >= dst->height) return true;
    if (width > dst->width - (uint32_t)dst_x) width = dst->width - (uint32_t)dst_x;
    if (height > dst->height - (uint32_t)dst_y) height = dst->height - (uint32_t)dst_y;

    if (width == 0U || height == 0U) return true;

    uint32_t src_stride = src->pitch / 4U;

    for (uint32_t r = 0; r < height; r++) {
        size_t src_off = (size_t)((uint32_t)src_y + r) * src_stride + (uint32_t)src_x;
        if (src->buffer_size > 0U && (src_off + width) * sizeof(uint32_t) > src->buffer_size) break;
        const uint32_t *src_line = &src->buffer[src_off];

        for (uint32_t c = 0; c < width; c++) {
            uint32_t color = src_line[c];
            if (src->format == (uint8_t)GFX_FORMAT_ABGR8888) {
                uint32_t red = (color >> 16) & 0xFFU;
                uint32_t blue = color & 0xFFU;
                color = (color & 0xFF00FF00U) | (blue << 16) | red;
            }
            gfx_blend_pixel(dst, (uint32_t)dst_x + c, (uint32_t)dst_y + r, color);
        }
    }
    return true;
}

/* Dirty Region Damage Tracking & Scheduled Frame Flushing */
void gfx_damage_add(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (width == 0U || height == 0U) return;
    if (x >= backbuffer_surface.width || y >= backbuffer_surface.height) return;
    if (width > backbuffer_surface.width - x) width = backbuffer_surface.width - x;
    if (height > backbuffer_surface.height - y) height = backbuffer_surface.height - y;

    uint32_t x2 = x + width;
    uint32_t y2 = y + height;

    if (!g_damage.dirty) {
        g_damage.dirty = true;
        g_damage.min_x = x;
        g_damage.min_y = y;
        g_damage.max_x = x2;
        g_damage.max_y = y2;
    } else {
        if (x < g_damage.min_x) g_damage.min_x = x;
        if (y < g_damage.min_y) g_damage.min_y = y;
        if (x2 > g_damage.max_x) g_damage.max_x = x2;
        if (y2 > g_damage.max_y) g_damage.max_y = y2;
    }
}

void gfx_damage_add_fullscreen(void) {
    g_damage.dirty = true;
    g_damage.min_x = 0U;
    g_damage.min_y = 0U;
    g_damage.max_x = backbuffer_surface.width;
    g_damage.max_y = backbuffer_surface.height;
}

bool gfx_damage_is_dirty(void) {
    return g_damage.dirty;
}

struct gfx_rect gfx_damage_get_bounds(void) {
    struct gfx_rect r = {0U, 0U, 0U, 0U};
    if (g_damage.dirty && g_damage.max_x >= g_damage.min_x && g_damage.max_y >= g_damage.min_y) {
        r.x = g_damage.min_x;
        r.y = g_damage.min_y;
        r.width = g_damage.max_x - g_damage.min_x;
        r.height = g_damage.max_y - g_damage.min_y;
    }
    return r;
}

void gfx_damage_clear(void) {
    g_damage.dirty = false;
    g_damage.min_x = 0U;
    g_damage.min_y = 0U;
    g_damage.max_x = 0U;
    g_damage.max_y = 0U;
}

void gfx_flush(void) {
    if (!g_damage.dirty) return;
    if (fb_surface.buffer == NULL || backbuffer_surface.buffer == NULL) return;
    if (fb_surface.buffer == backbuffer_surface.buffer) {
        gfx_damage_clear();
        return;
    }

    uint32_t w = g_damage.max_x - g_damage.min_x;
    uint32_t h = g_damage.max_y - g_damage.min_y;

    (void)gfx_blit(&fb_surface, (int32_t)g_damage.min_x, (int32_t)g_damage.min_y,
                   &backbuffer_surface, (int32_t)g_damage.min_x, (int32_t)g_damage.min_y,
                   w, h);

    gfx_damage_clear();
}

void gfx_flip(void) {
    gfx_damage_add_fullscreen();
    gfx_flush();
}

void gfx_draw_char(struct gfx_surface *surf, uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;
    if (x >= surf->width || y >= surf->height) return;

    if ((uint8_t)c < 32U || (uint8_t)c > 126U) c = ' ';
    const uint8_t *glyph = font_8x16[(uint8_t)c - 32U];

    uint32_t max_r = (16U > surf->height - y) ? (surf->height - y) : 16U;
    uint32_t max_c = (8U > surf->width - x) ? (surf->width - x) : 8U;

    for (uint32_t r = 0; r < max_r; r++) {
        uint8_t row = glyph[r];
        for (uint32_t col = 0; col < max_c; col++) {
            if ((row & (0x80U >> col)) != 0U) {
                gfx_blend_pixel(surf, x + col, y + r, color);
            }
        }
    }
}

void gfx_draw_string(struct gfx_surface *surf, uint32_t x, uint32_t y, const char *str, uint32_t color) {
    if (!gfx_surface_is_valid(surf) || str == NULL) return;
    uint32_t cx = x;
    uint32_t cy = y;

    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cx = x;
            cy += 16U;
            if (cy >= surf->height) break;
            continue;
        }
        if (cx < surf->width && cy < surf->height) {
            gfx_draw_char(surf, cx, cy, str[i], color);
        }
        cx += 8U;
    }
}

void gfx_draw_icon(struct gfx_surface *surf, uint32_t x, uint32_t y, enum gfx_icon icon, uint32_t color) {
    if (!gfx_surface_is_valid(surf)) return;

    switch (icon) {
        case GFX_ICON_TERMINAL:
            /* Terminal box with prompt '>_' */
            gfx_draw_glass_panel(surf, x, y, 16U, 16U, GFX_COLOR_EMERALD_DEEP);
            gfx_draw_char(surf, x + 2U, y, '>', color);
            gfx_draw_line(surf, (int32_t)(x + 9U), (int32_t)(y + 11U), (int32_t)(x + 13U), (int32_t)(y + 11U), color);
            break;
        case GFX_ICON_FILE_EXPLORER:
            /* Folder shape */
            gfx_draw_rect_filled(surf, x + 1U, y + 2U, 6U, 3U, color);
            gfx_draw_glass_panel(surf, x, y + 4U, 16U, 11U, GFX_COLOR_EMERALD_SURFACE);
            break;
        case GFX_ICON_TASK_MANAGER:
            /* Activity graph */
            gfx_draw_glass_panel(surf, x, y, 16U, 16U, GFX_COLOR_EMERALD_DEEP);
            gfx_draw_line(surf, (int32_t)(x + 2U), (int32_t)(y + 10U), (int32_t)(x + 5U), (int32_t)(y + 4U), color);
            gfx_draw_line(surf, (int32_t)(x + 5U), (int32_t)(y + 4U), (int32_t)(x + 9U), (int32_t)(y + 12U), color);
            gfx_draw_line(surf, (int32_t)(x + 9U), (int32_t)(y + 12U), (int32_t)(x + 14U), (int32_t)(y + 3U), color);
            break;
        case GFX_ICON_SETTINGS:
            /* Cog / gear */
            gfx_draw_glass_panel(surf, x + 3U, y + 3U, 10U, 10U, GFX_COLOR_EMERALD_SURFACE);
            gfx_draw_rect_outline(surf, x + 1U, y + 1U, 14U, 14U, color);
            gfx_draw_rect_filled(surf, x + 6U, y + 6U, 4U, 4U, color);
            break;
        case GFX_ICON_COMPUTER:
            /* Monitor */
            gfx_draw_rect_filled(surf, x + 1U, y + 2U, 14U, 9U, color);
            gfx_draw_rect_filled(surf, x + 3U, y + 4U, 10U, 5U, GFX_COLOR_EMERALD_DEEP);
            gfx_draw_line(surf, (int32_t)(x + 7U), (int32_t)(y + 11U), (int32_t)(x + 7U), (int32_t)(y + 13U), color);
            gfx_draw_line(surf, (int32_t)(x + 4U), (int32_t)(y + 14U), (int32_t)(x + 11U), (int32_t)(y + 14U), color);
            break;
        case GFX_ICON_SHUTDOWN:
            /* Power circle with vertical notch */
            gfx_draw_rect_outline(surf, x + 2U, y + 2U, 12U, 12U, color);
            gfx_draw_line(surf, (int32_t)(x + 7U), (int32_t)(y + 1U), (int32_t)(x + 7U), (int32_t)(y + 6U), color);
            break;
        case GFX_ICON_CALCULATOR:
            /* Calculator body with screen and 4 buttons */
            gfx_draw_glass_panel(surf, x + 1U, y + 1U, 14U, 14U, GFX_COLOR_EMERALD_BASE);
            gfx_draw_rect_outline(surf, x + 1U, y + 1U, 14U, 14U, color);
            gfx_draw_rect_filled(surf, x + 3U, y + 3U, 10U, 3U, GFX_COLOR_EMERALD_DEEP);
            gfx_draw_rect_filled(surf, x + 3U, y + 8U, 3U, 2U, color);
            gfx_draw_rect_filled(surf, x + 7U, y + 8U, 3U, 2U, color);
            gfx_draw_rect_filled(surf, x + 3U, y + 11U, 3U, 2U, color);
            gfx_draw_rect_filled(surf, x + 7U, y + 11U, 6U, 2U, GFX_COLOR_EMERALD_HIGHLIGHT);
            break;
        case GFX_ICON_CALENDAR:
            /* Calendar sheet with red/emerald top bar and date grid */
            gfx_draw_glass_panel(surf, x + 1U, y + 2U, 14U, 13U, GFX_COLOR_EMERALD_BASE);
            gfx_draw_rect_outline(surf, x + 1U, y + 2U, 14U, 13U, color);
            gfx_draw_rect_filled(surf, x + 1U, y + 2U, 14U, 4U, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_line(surf, (int32_t)(x + 4U), (int32_t)y, (int32_t)(x + 4U), (int32_t)(y + 2U), color);
            gfx_draw_line(surf, (int32_t)(x + 11U), (int32_t)y, (int32_t)(x + 11U), (int32_t)(y + 2U), color);
            gfx_draw_rect_filled(surf, x + 4U, y + 8U, 2U, 2U, color);
            gfx_draw_rect_filled(surf, x + 8U, y + 8U, 2U, 2U, color);
            gfx_draw_rect_filled(surf, x + 6U, y + 11U, 4U, 3U, GFX_COLOR_EMERALD_HIGHLIGHT);
            break;
        case GFX_ICON_DAWN_LOGO:
        default:
            /* Emerald diamond / dawn sun symbol */
            gfx_draw_line(surf, (int32_t)(x + 7U), (int32_t)(y + 1U), (int32_t)(x + 14U), (int32_t)(y + 7U), color);
            gfx_draw_line(surf, (int32_t)(x + 14U), (int32_t)(y + 7U), (int32_t)(x + 7U), (int32_t)(y + 14U), color);
            gfx_draw_line(surf, (int32_t)(x + 7U), (int32_t)(y + 14U), (int32_t)x, (int32_t)(y + 7U), color);
            gfx_draw_line(surf, (int32_t)x, (int32_t)(y + 7U), (int32_t)(x + 7U), (int32_t)(y + 1U), color);
            gfx_draw_rect_filled(surf, x + 6U, y + 6U, 3U, 3U, GFX_COLOR_EMERALD_HIGHLIGHT);
            break;
    }
}

/* ========================================================================= */
/* Deterministic Headless Self-Test Harness for Phase D1                     */
/* ========================================================================= */

static uint32_t test_surface_buf_a[128 * 128];
static uint32_t test_surface_buf_b[128 * 128];

bool gfx_self_test(void) {
    if (!gfx_initialize()) return false;

    /* --- Subtest 1: Framebuffer Metadata Validation --- */
    {
        /* Null pointer rejection */
        if (gfx_validate_framebuffer(NULL)) return false;

        struct limine_framebuffer invalid_fb = {
            .address = (void *)0xFFFF8000000B8000ULL,
            .width = 1024ULL,
            .height = 768ULL,
            .pitch = 1024ULL * 4ULL,
            .bpp = 32U,
            .memory_model = 1U,
            .red_mask_size = 8U,
            .red_mask_shift = 16U,
            .green_mask_size = 8U,
            .green_mask_shift = 8U,
            .blue_mask_size = 8U,
            .blue_mask_shift = 0U,
            .unused = {0},
            .edid_size = 0ULL,
            .edid = NULL,
            .mode_count = 0ULL,
            .modes = NULL
        };

        /* Null address */
        invalid_fb.address = NULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;

        /* Lower canonical / unmapped address */
        invalid_fb.address = (void *)0x0000000000100000ULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.address = (void *)0xFFFF8000000B8000ULL;

        /* Zero width / height */
        invalid_fb.width = 0ULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.width = 1024ULL;

        invalid_fb.height = 0ULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.height = 768ULL;

        /* Non-32 bpp */
        invalid_fb.bpp = 16U;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.bpp = 24U;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.bpp = 32U;

        /* Pitch smaller than width * 4 */
        invalid_fb.pitch = (1024ULL * 4ULL) - 4ULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;

        /* Unaligned pitch */
        invalid_fb.pitch = (1024ULL * 4ULL) + 1ULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;
        invalid_fb.pitch = 1024ULL * 4ULL;

        /* Pitch * height arithmetic overflow */
        invalid_fb.pitch = 0xFFFFFFFF00000000ULL;
        invalid_fb.height = 0xFFFFFFFFULL;
        if (gfx_validate_framebuffer(&invalid_fb)) return false;

        /* Valid synthetic FB */
        invalid_fb.pitch = 1024ULL * 4ULL;
        invalid_fb.height = 768ULL;
        if (!gfx_validate_framebuffer(&invalid_fb)) return false;

        /* Validate live QEMU Limine FB if reported */
        if (framebuffer_response_valid()) {
            const struct limine_framebuffer *live = limine_framebuffer_request.response->framebuffers[0];
            if (!gfx_validate_framebuffer(live)) return false;
        }
    }

    /* --- Subtest 2: Safe Surface Creation and Validation --- */
    struct gfx_surface surf_a;
    struct gfx_surface surf_b;

    if (!gfx_surface_init(&surf_a, test_surface_buf_a, 64U, 64U, 64U * 4U,
                          sizeof(test_surface_buf_a), false, (uint8_t)GFX_FORMAT_ARGB8888)) {
        return false;
    }
    if (!gfx_surface_init(&surf_b, test_surface_buf_b, 64U, 64U, 64U * 4U,
                          sizeof(test_surface_buf_b), false, (uint8_t)GFX_FORMAT_ARGB8888)) {
        return false;
    }

    /* Reject invalid surface parameters */
    if (gfx_surface_init(NULL, test_surface_buf_a, 64U, 64U, 64U * 4U, sizeof(test_surface_buf_a), false, 0)) return false;
    if (gfx_surface_init(&surf_a, NULL, 64U, 64U, 64U * 4U, sizeof(test_surface_buf_a), false, 0)) return false;
    if (gfx_surface_init(&surf_a, test_surface_buf_a, 0U, 64U, 64U * 4U, sizeof(test_surface_buf_a), false, 0)) return false;
    if (gfx_surface_init(&surf_a, test_surface_buf_a, 64U, 64U, 16U, sizeof(test_surface_buf_a), false, 0)) return false;

    /* --- Subtest 3: Pixel Plotting & Alpha Blending --- */
    gfx_clear(&surf_a, GFX_COLOR_BLACK);
    if (surf_a.buffer[0] != GFX_COLOR_BLACK) return false;

    gfx_put_pixel(&surf_a, 10U, 10U, GFX_COLOR_WHITE);
    if (surf_a.buffer[10U * 64U + 10U] != GFX_COLOR_WHITE) return false;

    /* Alpha blend 50% white over black */
    gfx_put_pixel(&surf_a, 20U, 20U, GFX_COLOR_BLACK);
    gfx_blend_pixel(&surf_a, 20U, 20U, 0x80FFFFFFU);
    uint32_t blended = surf_a.buffer[20U * 64U + 20U];
    uint8_t red = (uint8_t)((blended >> 16) & 0xFFU);
    if (red < 120U || red > 135U) return false;

    /* Out of bounds pixel plotting must be safely ignored */
    gfx_put_pixel(&surf_a, 64U, 64U, GFX_COLOR_WHITE);
    gfx_put_pixel(&surf_a, 0xFFFFFFFFU, 0xFFFFFFFFU, GFX_COLOR_WHITE);
    gfx_blend_pixel(&surf_a, 64U, 64U, GFX_COLOR_WHITE);
    gfx_blend_pixel(&surf_a, 0xFFFFFFFFU, 0xFFFFFFFFU, GFX_COLOR_WHITE);

    /* --- Subtest 4: Clipped Primitives & Integer Overflow Safety --- */
    /* Draw oversized rect that would overflow uint32_t if added without subtraction bounds */
    gfx_clear(&surf_a, GFX_COLOR_BLACK);
    gfx_draw_rect_filled(&surf_a, 50U, 50U, 0xFFFFFFFFU, 0xFFFFFFFFU, GFX_COLOR_EMERALD_ACCENT);
    /* Should clip to remaining (14x14) */
    if (surf_a.buffer[50U * 64U + 50U] != GFX_COLOR_EMERALD_ACCENT) return false;
    if (surf_a.buffer[63U * 64U + 63U] != GFX_COLOR_EMERALD_ACCENT) return false;
    if (surf_a.buffer[49U * 64U + 50U] != GFX_COLOR_BLACK) return false;

    /* Signed rect drawing with negative coordinates (partially offscreen) */
    gfx_clear(&surf_a, GFX_COLOR_BLACK);
    gfx_draw_rect_filled_signed(&surf_a, -10, -10, 20, 20, GFX_COLOR_WHITE);
    /* Clipped visible region is (0, 0) to (9, 9) */
    if (surf_a.buffer[0] != GFX_COLOR_WHITE) return false;
    if (surf_a.buffer[9U * 64U + 9U] != GFX_COLOR_WHITE) return false;
    if (surf_a.buffer[10U * 64U + 10U] != GFX_COLOR_BLACK) return false;

    /* Completely offscreen signed rect */
    gfx_draw_rect_filled_signed(&surf_a, -100, -100, 20, 20, GFX_COLOR_WHITE);
    gfx_draw_rect_filled_signed(&surf_a, 100, 100, 20, 20, GFX_COLOR_WHITE);

    /* Outline drawing */
    gfx_clear(&surf_a, GFX_COLOR_BLACK);
    gfx_draw_rect_outline(&surf_a, 10U, 10U, 20U, 20U, GFX_COLOR_WHITE);
    if (surf_a.buffer[10U * 64U + 10U] != GFX_COLOR_WHITE) return false;
    if (surf_a.buffer[15U * 64U + 15U] != GFX_COLOR_BLACK) return false;

    /* Line drawing */
    gfx_draw_line(&surf_a, 0, 0, 63, 63, GFX_COLOR_EMERALD_HIGHLIGHT);
    if (surf_a.buffer[0] != GFX_COLOR_EMERALD_HIGHLIGHT) return false;
    if (surf_a.buffer[63U * 64U + 63U] != GFX_COLOR_EMERALD_HIGHLIGHT) return false;

    /* --- Subtest 5: Clipped Blit and Alpha Blit --- */
    gfx_clear(&surf_a, GFX_COLOR_EMERALD_PRIMARY);
    gfx_clear(&surf_b, GFX_COLOR_BLACK);

    /* Normal blit from surf_a (10, 10, 20x20) to surf_b (5, 5) */
    if (!gfx_blit(&surf_b, 5, 5, &surf_a, 10, 10, 20U, 20U)) return false;
    if (surf_b.buffer[5U * 64U + 5U] != GFX_COLOR_EMERALD_PRIMARY) return false;
    if (surf_b.buffer[4U * 64U + 5U] != GFX_COLOR_BLACK) return false;

    /* Negative destination blit */
    gfx_clear(&surf_b, GFX_COLOR_BLACK);
    if (!gfx_blit(&surf_b, -5, -5, &surf_a, 0, 0, 20U, 20U)) return false;
    /* Clipped destination starts at (0, 0), getting source (5, 5) */
    if (surf_b.buffer[0] != GFX_COLOR_EMERALD_PRIMARY) return false;

    /* Out-of-bounds blit returns true and does not corrupt */
    if (!gfx_blit(&surf_b, 200, 200, &surf_a, 0, 0, 20U, 20U)) return false;
    if (!gfx_blit(&surf_b, 0, 0, &surf_a, 200, 200, 20U, 20U)) return false;

    /* Alpha blit */
    gfx_clear(&surf_a, 0x80FFFFFFU); /* 50% white */
    gfx_clear(&surf_b, GFX_COLOR_BLACK);
    if (!gfx_blit_alpha(&surf_b, 0, 0, &surf_a, 0, 0, 10U, 10U)) return false;
    uint32_t alpha_pixel = surf_b.buffer[0];
    uint8_t alpha_red = (uint8_t)((alpha_pixel >> 16) & 0xFFU);
    if (alpha_red < 120U || alpha_red > 135U) return false;

    /* --- Subtest 6: Damage Tracking and Scheduled Flush --- */
    gfx_damage_clear();
    if (gfx_damage_is_dirty()) return false;

    gfx_damage_add(10U, 10U, 20U, 20U);
    if (!gfx_damage_is_dirty()) return false;
    struct gfx_rect bounds = gfx_damage_get_bounds();
    if (bounds.x != 10U || bounds.y != 10U || bounds.width != 20U || bounds.height != 20U) return false;

    /* Expand damage */
    gfx_damage_add(5U, 5U, 50U, 50U);
    bounds = gfx_damage_get_bounds();
    if (bounds.x != 5U || bounds.y != 5U || bounds.width != 50U || bounds.height != 50U) return false;

    gfx_damage_clear();
    if (gfx_damage_is_dirty()) return false;

    /* --- Subtest 7: Glyph & String Rendering --- */
    gfx_clear(&surf_a, GFX_COLOR_BLACK);
    gfx_draw_char(&surf_a, 10U, 10U, 'A', GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(&surf_a, 0U, 0U, "DAWN-OS\nGFX", GFX_COLOR_WHITE);

    /* Render at boundary edge to verify safe glyph clipping */
    gfx_draw_char(&surf_a, 60U, 60U, 'X', GFX_COLOR_WHITE);
    gfx_draw_string(&surf_a, 50U, 55U, "LONGSTRING", GFX_COLOR_WHITE);

    /* --- Subtest 8: Glass Panel & Icons --- */
    gfx_draw_glass_panel(&surf_a, 5U, 5U, 30U, 20U, GFX_COLOR_GLASS_PANEL);
    gfx_draw_icon(&surf_a, 40U, 40U, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_icon(&surf_a, 40U, 20U, GFX_ICON_TERMINAL, GFX_COLOR_EMERALD_HIGHLIGHT);
    gfx_draw_icon(&surf_a, 20U, 40U, GFX_ICON_FILE_EXPLORER, GFX_COLOR_EMERALD_ACCENT);

    return true;
}
