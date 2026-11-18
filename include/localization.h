/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_LOCALIZATION_H
#define DAWN_LOCALIZATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum system_locale {
    LOCALE_EN_US = 0,
    LOCALE_ES_ES = 1,
    LOCALE_FR_FR = 2,
    LOCALE_DE_DE = 3,
    LOCALE_JA_JP = 4,
    LOCALE_COUNT = 5
};

enum keyboard_layout {
    KBD_LAYOUT_US_QWERTY = 0,
    KBD_LAYOUT_UK_QWERTY = 1,
    KBD_LAYOUT_FR_AZERTY = 2,
    KBD_LAYOUT_DE_QWERTZ = 3,
    KBD_LAYOUT_DVORAK = 4,
    KBD_LAYOUT_COUNT = 5
};

enum localized_string_id {
    STR_WELCOME = 0,
    STR_TERMINAL = 1,
    STR_FILE_EXPLORER = 2,
    STR_TASK_MANAGER = 3,
    STR_SETTINGS = 4,
    STR_SHUTDOWN = 5,
    STR_REBOOT = 6,
    STR_APPLY = 7,
    STR_CANCEL = 8,
    STR_OK = 9,
    STR_STRING_COUNT = 10
};

struct accessibility_settings {
    bool high_contrast;
    uint8_t font_scale; /* 1 = normal, 2 = 2x large */
    bool screen_reader_active;
    bool large_cursor;
};

/* Core Localization API */
bool localization_initialize(void);
bool locale_set(enum system_locale loc);
enum system_locale locale_get(void);
const char *locale_get_string(enum localized_string_id id);

/* Keyboard Layout Translation */
bool keymap_set(enum keyboard_layout layout);
enum keyboard_layout keymap_get(void);
char keymap_translate(uint8_t scancode, bool shift);

/* Accessibility Features */
void accessibility_set_high_contrast(bool enable);
bool accessibility_is_high_contrast(void);
void accessibility_set_font_scale(uint8_t scale);
uint8_t accessibility_get_font_scale(void);
void screen_reader_announce(const char *text);

/* Self-test */
bool localization_self_test(void);

#endif /* DAWN_LOCALIZATION_H */
