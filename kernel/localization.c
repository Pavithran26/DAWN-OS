/* SPDX-License-Identifier: Unlicense */
#include "localization.h"

static enum system_locale current_locale = LOCALE_EN_US;
static enum keyboard_layout current_layout = KBD_LAYOUT_US_QWERTY;
static struct accessibility_settings a11y_settings = {
    .high_contrast = false,
    .font_scale = 1,
    .screen_reader_active = false,
    .large_cursor = false
};
static bool localization_initialized = false;

/* Multi-language string catalogs */
static const char * const string_table[LOCALE_COUNT][STR_STRING_COUNT] = {
    [LOCALE_EN_US] = {
        [STR_WELCOME]       = "Welcome to DAWN-OS",
        [STR_TERMINAL]      = "Terminal",
        [STR_FILE_EXPLORER] = "File Explorer",
        [STR_TASK_MANAGER]  = "Task Manager",
        [STR_SETTINGS]      = "Settings",
        [STR_SHUTDOWN]      = "Shut Down",
        [STR_REBOOT]        = "Restart",
        [STR_APPLY]         = "Apply",
        [STR_CANCEL]        = "Cancel",
        [STR_OK]            = "OK"
    },
    [LOCALE_ES_ES] = {
        [STR_WELCOME]       = "Bienvenido a DAWN-OS",
        [STR_TERMINAL]      = "Terminal",
        [STR_FILE_EXPLORER] = "Explorador de Archivos",
        [STR_TASK_MANAGER]  = "Administrador de Tareas",
        [STR_SETTINGS]      = "Configuracion",
        [STR_SHUTDOWN]      = "Apagar",
        [STR_REBOOT]        = "Reiniciar",
        [STR_APPLY]         = "Aplicar",
        [STR_CANCEL]        = "Cancelar",
        [STR_OK]            = "Aceptar"
    },
    [LOCALE_FR_FR] = {
        [STR_WELCOME]       = "Bienvenue sur DAWN-OS",
        [STR_TERMINAL]      = "Terminal",
        [STR_FILE_EXPLORER] = "Gestionnaire de Fichiers",
        [STR_TASK_MANAGER]  = "Gestionnaire des Taches",
        [STR_SETTINGS]      = "Parametres",
        [STR_SHUTDOWN]      = "Eteindre",
        [STR_REBOOT]        = "Redemarrer",
        [STR_APPLY]         = "Appliquer",
        [STR_CANCEL]        = "Annuler",
        [STR_OK]            = "OK"
    },
    [LOCALE_DE_DE] = {
        [STR_WELCOME]       = "Willkommen bei DAWN-OS",
        [STR_TERMINAL]      = "Terminal",
        [STR_FILE_EXPLORER] = "Datei-Explorer",
        [STR_TASK_MANAGER]  = "Task-Manager",
        [STR_SETTINGS]      = "Einstellungen",
        [STR_SHUTDOWN]      = "Herunterfahren",
        [STR_REBOOT]        = "Neu starten",
        [STR_APPLY]         = "Anwenden",
        [STR_CANCEL]        = "Abbrechen",
        [STR_OK]            = "OK"
    },
    [LOCALE_JA_JP] = {
        [STR_WELCOME]       = "DAWN-OS e Yokoso",
        [STR_TERMINAL]      = "Taminaru",
        [STR_FILE_EXPLORER] = "Fairu Ekusupurora",
        [STR_TASK_MANAGER]  = "Tasuku Maneja",
        [STR_SETTINGS]      = "Settei",
        [STR_SHUTDOWN]      = "Shattodaun",
        [STR_REBOOT]        = "Saikido",
        [STR_APPLY]         = "Tekiyo",
        [STR_CANCEL]        = "Kyanseru",
        [STR_OK]            = "OK"
    }
};

/* Keyboard Scancode Set 1 Translation */
char keymap_translate(uint8_t scancode, bool shift) {
    if (scancode > 0x58U) return '\0';

    switch (current_layout) {
        case KBD_LAYOUT_DE_QWERTZ:
            if (scancode == 0x15U) return shift ? 'Z' : 'z'; /* Y -> Z */
            if (scancode == 0x2CU) return shift ? 'Y' : 'y'; /* Z -> Y */
            break;

        case KBD_LAYOUT_FR_AZERTY:
            if (scancode == 0x10U) return shift ? 'A' : 'a'; /* Q -> A */
            if (scancode == 0x11U) return shift ? 'Z' : 'z'; /* W -> Z */
            if (scancode == 0x1EU) return shift ? 'Q' : 'q'; /* A -> Q */
            if (scancode == 0x2CU) return shift ? 'W' : 'w'; /* Z -> W */
            if (scancode == 0x30U) return shift ? 'B' : 'b';
            if (scancode == 0x32U) return shift ? ',' : ';';
            break;

        case KBD_LAYOUT_DVORAK:
            if (scancode == 0x10U) return shift ? '"' : '\'';
            if (scancode == 0x11U) return shift ? '<' : ',';
            if (scancode == 0x12U) return shift ? '>' : '.';
            if (scancode == 0x13U) return shift ? 'P' : 'p';
            if (scancode == 0x14U) return shift ? 'Y' : 'y';
            if (scancode == 0x15U) return shift ? 'F' : 'f';
            break;

        case KBD_LAYOUT_US_QWERTY:
        case KBD_LAYOUT_UK_QWERTY:
        default:
            break;
    }

    /* Standard US QWERTY base */
    static const char unshifted[0x3AU] = {
        0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0,   ' '
    };

    static const char shifted[0x3AU] = {
        0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0,   ' '
    };

    if (scancode < 0x3AU) {
        return shift ? shifted[scancode] : unshifted[scancode];
    }

    return '\0';
}

bool localization_initialize(void) {
    if (localization_initialized) return true;

    current_locale = LOCALE_EN_US;
    current_layout = KBD_LAYOUT_US_QWERTY;
    a11y_settings.high_contrast = false;
    a11y_settings.font_scale = 1;
    a11y_settings.screen_reader_active = false;
    a11y_settings.large_cursor = false;

    localization_initialized = true;
    return true;
}

bool locale_set(enum system_locale loc) {
    if (loc < LOCALE_COUNT) {
        current_locale = loc;
        return true;
    }
    return false;
}

enum system_locale locale_get(void) {
    return current_locale;
}

const char *locale_get_string(enum localized_string_id id) {
    if (id < STR_STRING_COUNT && current_locale < LOCALE_COUNT) {
        return string_table[current_locale][id];
    }
    return "";
}

bool keymap_set(enum keyboard_layout layout) {
    if (layout < KBD_LAYOUT_COUNT) {
        current_layout = layout;
        return true;
    }
    return false;
}

enum keyboard_layout keymap_get(void) {
    return current_layout;
}

void accessibility_set_high_contrast(bool enable) {
    a11y_settings.high_contrast = enable;
}

bool accessibility_is_high_contrast(void) {
    return a11y_settings.high_contrast;
}

void accessibility_set_font_scale(uint8_t scale) {
    a11y_settings.font_scale = (scale >= 2) ? 2 : 1;
}

uint8_t accessibility_get_font_scale(void) {
    return a11y_settings.font_scale;
}

void screen_reader_announce(const char *text) {
    (void)text;
    /* Hook ready for serial speech synthesizer or accessibility daemon */
}

/* Self-test */
static inline bool str_match(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

bool localization_self_test(void) {
    if (!localization_initialize()) return false;

    /* Test default locale string */
    const char *en_term = locale_get_string(STR_TERMINAL);
    if (!str_match(en_term, "Terminal")) return false;

    /* Test locale change to Spanish */
    if (!locale_set(LOCALE_ES_ES)) return false;
    const char *es_welcome = locale_get_string(STR_WELCOME);
    if (!str_match(es_welcome, "Bienvenido a DAWN-OS")) return false;

    /* Restore English */
    locale_set(LOCALE_EN_US);

    /* Test keyboard layout translation: Scancode 0x15 ('y' in US QWERTY, 'z' in German QWERTZ) */
    keymap_set(KBD_LAYOUT_US_QWERTY);
    if (keymap_translate(0x15U, false) != 'y') return false;

    keymap_set(KBD_LAYOUT_DE_QWERTZ);
    if (keymap_translate(0x15U, false) != 'z') return false;

    /* French AZERTY: Scancode 0x10 is 'a', 0x1E is 'q' */
    keymap_set(KBD_LAYOUT_FR_AZERTY);
    if (keymap_translate(0x10U, false) != 'a') return false;
    if (keymap_translate(0x1EU, false) != 'q') return false;

    keymap_set(KBD_LAYOUT_US_QWERTY);

    /* Accessibility tests */
    accessibility_set_high_contrast(true);
    if (!accessibility_is_high_contrast()) return false;
    accessibility_set_high_contrast(false);

    accessibility_set_font_scale(2);
    if (accessibility_get_font_scale() != 2) return false;
    accessibility_set_font_scale(1);

    return true;
}
