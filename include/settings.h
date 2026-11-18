/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SETTINGS_H
#define DAWN_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "window_manager.h"
#include "gfx.h"

#define SETTINGS_PATH_DEFAULT "/boot/system.cfg"

enum power_profile {
    POWER_PROFILE_BALANCED = 0,
    POWER_PROFILE_PERFORMANCE = 1,
    POWER_PROFILE_POWER_SAVER = 2
};

enum settings_tab {
    TAB_DISPLAY = 0,
    TAB_INPUT = 1,
    TAB_AUDIO = 2,
    TAB_NETWORK = 3,
    TAB_TIME = 4,
    TAB_APPEARANCE = 5,
    TAB_ABOUT = 6,
    TAB_COUNT = 7
};

struct system_settings {
    /* Display */
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t color_depth;
    uint32_t refresh_rate;

    /* Input */
    uint16_t kbd_repeat_delay_ms;
    uint16_t kbd_repeat_rate_cps;
    uint8_t  mouse_sensitivity; /* 1..10 */
    bool     mouse_acceleration;

    /* Audio */
    uint8_t  master_volume; /* 0..100 */
    bool     audio_muted;

    /* Network */
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    bool     dhcp_enabled;

    /* Date & Time */
    int16_t  timezone_offset_minutes; /* e.g. -300 for UTC-5, +330 for UTC+5:30 */
    bool     use_24hr_clock;

    /* Appearance */
    uint32_t accent_color;
    uint8_t  glass_opacity; /* 0..255 */
    uint8_t  wallpaper_id;

    /* Power */
    enum power_profile power_profile;
    uint16_t screen_timeout_minutes;
};

/* Core Subsystem Functions */
bool settings_initialize(void);
struct system_settings *settings_get_active(void);
void settings_set_defaults(struct system_settings *s);
bool settings_apply(const struct system_settings *s);

/* Persistence (Serialization / Deserialization) */
size_t settings_serialize(const struct system_settings *s, char *buf, size_t max_len);
bool settings_deserialize(struct system_settings *s, const char *buf, size_t len);
bool settings_load_file(const char *path);
bool settings_save_file(const char *path);

/* GUI Settings Application Window */
bool settings_window_open(void);
void settings_window_close(void);
void settings_window_render(void);
void settings_window_handle_click(int32_t local_x, int32_t local_y);
void settings_window_switch_tab(enum settings_tab tab);

/* Self-test */
bool settings_self_test(void);

#endif /* DAWN_SETTINGS_H */
