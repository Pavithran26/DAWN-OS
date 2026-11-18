/* SPDX-License-Identifier: Unlicense */
#include "settings.h"
#include "vfs.h"
#include "audio.h"
#include "net.h"
#include "window_manager.h"
#include "gfx.h"

static struct system_settings active_settings;
static bool settings_initialized = false;
static int settings_window_id = -1;
static enum settings_tab current_tab = TAB_DISPLAY;
static uint32_t settings_win_buffer[560 * 380];

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static size_t u32_to_str(uint32_t val, char *buf) {
    char tmp[16];
    size_t pos = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    while (val > 0) {
        tmp[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (size_t i = 0; i < pos; i++) {
        buf[i] = tmp[pos - 1 - i];
    }
    buf[pos] = '\0';
    return pos;
}

static uint32_t str_to_u32(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

static void ip_to_str(uint32_t ip_net_order, char *buf) {
    uint32_t ip = ntohl(ip_net_order);
    uint8_t b1 = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b2 = (uint8_t)((ip >> 16) & 0xFF);
    uint8_t b3 = (uint8_t)((ip >> 8) & 0xFF);
    uint8_t b4 = (uint8_t)(ip & 0xFF);

    size_t p = 0;
    p += u32_to_str(b1, buf + p);
    buf[p++] = '.';
    p += u32_to_str(b2, buf + p);
    buf[p++] = '.';
    p += u32_to_str(b3, buf + p);
    buf[p++] = '.';
    p += u32_to_str(b4, buf + p);
    buf[p] = '\0';
}

static uint32_t str_to_ip(const char *s) {
    uint32_t bytes[4] = {0};
    size_t part = 0;
    while (*s != '\0' && part < 4) {
        if (*s >= '0' && *s <= '9') {
            bytes[part] = bytes[part] * 10 + (uint32_t)(*s - '0');
        } else if (*s == '.') {
            part++;
        }
        s++;
    }
    uint32_t host_ip = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
    return htonl(host_ip);
}

void settings_set_defaults(struct system_settings *s) {
    if (s == NULL) return;

    /* Display */
    s->screen_width = 1024;
    s->screen_height = 768;
    s->color_depth = 32;
    s->refresh_rate = 60;

    /* Input */
    s->kbd_repeat_delay_ms = 250;
    s->kbd_repeat_rate_cps = 30;
    s->mouse_sensitivity = 5;
    s->mouse_acceleration = true;

    /* Audio */
    s->master_volume = 80;
    s->audio_muted = false;

    /* Network */
    s->ip_address = htonl(0x0A00020FU); /* 10.0.2.15 */
    s->netmask = htonl(0xFFFFFF00U);    /* 255.255.255.0 */
    s->gateway = htonl(0x0A000202U);    /* 10.0.2.2 */
    s->dns_server = htonl(0x08080808U);  /* 8.8.8.8 */
    s->dhcp_enabled = false;

    /* Date & Time */
    s->timezone_offset_minutes = 0;
    s->use_24hr_clock = true;

    /* Appearance */
    s->accent_color = GFX_COLOR_EMERALD_PRIMARY;
    s->glass_opacity = 200;
    s->wallpaper_id = 0;

    /* Power */
    s->power_profile = POWER_PROFILE_BALANCED;
    s->screen_timeout_minutes = 15;
}

bool settings_apply(const struct system_settings *s) {
    if (s == NULL) return false;

    /* Clamp values */
    active_settings = *s;
    if (active_settings.master_volume > 100) active_settings.master_volume = 100;
    if (active_settings.mouse_sensitivity < 1) active_settings.mouse_sensitivity = 1;
    if (active_settings.mouse_sensitivity > 10) active_settings.mouse_sensitivity = 10;
    if (active_settings.power_profile > POWER_PROFILE_POWER_SAVER) {
        active_settings.power_profile = POWER_PROFILE_BALANCED;
    }

    /* Apply to Audio subsystem */
    audio_set_master_volume(active_settings.master_volume);
    audio_set_muted(active_settings.audio_muted);

    /* Apply to Network subsystem */
    struct net_device *eth0 = net_get_default_device();
    if (eth0 != NULL) {
        eth0->ip = active_settings.ip_address;
        eth0->netmask = active_settings.netmask;
        eth0->gateway = active_settings.gateway;
    }

    return true;
}

bool settings_initialize(void) {
    if (settings_initialized) return true;

    settings_set_defaults(&active_settings);

    /* Attempt loading from persistent file; fallback to defaults if not present */
    (void)settings_load_file(SETTINGS_PATH_DEFAULT);
    settings_apply(&active_settings);

    settings_initialized = true;
    return true;
}

struct system_settings *settings_get_active(void) {
    if (!settings_initialized) settings_initialize();
    return &active_settings;
}

/* Text Serialization */
size_t settings_serialize(const struct system_settings *s, char *buf, size_t max_len) {
    if (s == NULL || buf == NULL || max_len == 0) return 0;

    char line[128];
    size_t total = 0;

    #define APPEND_KEY_U32(key, val) do { \
        size_t lp = 0; \
        for (const char *k = key "="; *k; k++) line[lp++] = *k; \
        lp += u32_to_str(val, line + lp); \
        line[lp++] = '\n'; line[lp] = '\0'; \
        for (size_t c = 0; c < lp && total + 1 < max_len; c++) buf[total++] = line[c]; \
    } while (0)

    #define APPEND_KEY_IP(key, val) do { \
        size_t lp = 0; \
        for (const char *k = key "="; *k; k++) line[lp++] = *k; \
        ip_to_str(val, line + lp); \
        while (line[lp]) lp++; \
        line[lp++] = '\n'; line[lp] = '\0'; \
        for (size_t c = 0; c < lp && total + 1 < max_len; c++) buf[total++] = line[c]; \
    } while (0)

    APPEND_KEY_U32("display.width", s->screen_width);
    APPEND_KEY_U32("display.height", s->screen_height);
    APPEND_KEY_U32("display.depth", s->color_depth);
    APPEND_KEY_U32("display.refresh", s->refresh_rate);
    APPEND_KEY_U32("input.delay", s->kbd_repeat_delay_ms);
    APPEND_KEY_U32("input.rate", s->kbd_repeat_rate_cps);
    APPEND_KEY_U32("input.sensitivity", (uint32_t)s->mouse_sensitivity);
    APPEND_KEY_U32("input.accel", s->mouse_acceleration ? 1U : 0U);
    APPEND_KEY_U32("audio.volume", (uint32_t)s->master_volume);
    APPEND_KEY_U32("audio.muted", s->audio_muted ? 1U : 0U);
    APPEND_KEY_IP("net.ip", s->ip_address);
    APPEND_KEY_IP("net.netmask", s->netmask);
    APPEND_KEY_IP("net.gateway", s->gateway);
    APPEND_KEY_IP("net.dns", s->dns_server);
    APPEND_KEY_U32("net.dhcp", s->dhcp_enabled ? 1U : 0U);
    APPEND_KEY_U32("appearance.accent", s->accent_color);
    APPEND_KEY_U32("appearance.opacity", (uint32_t)s->glass_opacity);
    APPEND_KEY_U32("power.profile", (uint32_t)s->power_profile);
    APPEND_KEY_U32("power.timeout", (uint32_t)s->screen_timeout_minutes);

    buf[total] = '\0';
    return total;
}

bool settings_deserialize(struct system_settings *s, const char *buf, size_t len) {
    if (s == NULL || buf == NULL || len == 0) return false;

    settings_set_defaults(s);

    size_t pos = 0;
    while (pos < len && buf[pos] != '\0') {
        /* Read key */
        char key[64] = {0};
        size_t kp = 0;
        while (pos < len && buf[pos] != '=' && buf[pos] != '\n' && buf[pos] != '\0' && kp + 1 < sizeof(key)) {
            key[kp++] = buf[pos++];
        }
        key[kp] = '\0';

        if (pos < len && buf[pos] == '=') {
            pos++; /* Skip '=' */
            /* Read value */
            char val[64] = {0};
            size_t vp = 0;
            while (pos < len && buf[pos] != '\n' && buf[pos] != '\r' && buf[pos] != '\0' && vp + 1 < sizeof(val)) {
                val[vp++] = buf[pos++];
            }
            val[vp] = '\0';

            /* Parse keys */
            if (str_equal(key, "display.width")) s->screen_width = str_to_u32(val);
            else if (str_equal(key, "display.height")) s->screen_height = str_to_u32(val);
            else if (str_equal(key, "display.depth")) s->color_depth = str_to_u32(val);
            else if (str_equal(key, "display.refresh")) s->refresh_rate = str_to_u32(val);
            else if (str_equal(key, "input.delay")) s->kbd_repeat_delay_ms = (uint16_t)str_to_u32(val);
            else if (str_equal(key, "input.rate")) s->kbd_repeat_rate_cps = (uint16_t)str_to_u32(val);
            else if (str_equal(key, "input.sensitivity")) s->mouse_sensitivity = (uint8_t)str_to_u32(val);
            else if (str_equal(key, "input.accel")) s->mouse_acceleration = (str_to_u32(val) != 0);
            else if (str_equal(key, "audio.volume")) s->master_volume = (uint8_t)str_to_u32(val);
            else if (str_equal(key, "audio.muted")) s->audio_muted = (str_to_u32(val) != 0);
            else if (str_equal(key, "net.ip")) s->ip_address = str_to_ip(val);
            else if (str_equal(key, "net.netmask")) s->netmask = str_to_ip(val);
            else if (str_equal(key, "net.gateway")) s->gateway = str_to_ip(val);
            else if (str_equal(key, "net.dns")) s->dns_server = str_to_ip(val);
            else if (str_equal(key, "net.dhcp")) s->dhcp_enabled = (str_to_u32(val) != 0);
            else if (str_equal(key, "appearance.accent")) s->accent_color = str_to_u32(val);
            else if (str_equal(key, "appearance.opacity")) s->glass_opacity = (uint8_t)str_to_u32(val);
            else if (str_equal(key, "power.profile")) s->power_profile = (enum power_profile)str_to_u32(val);
            else if (str_equal(key, "power.timeout")) s->screen_timeout_minutes = (uint16_t)str_to_u32(val);
        }

        while (pos < len && (buf[pos] == '\n' || buf[pos] == '\r')) {
            pos++;
        }
    }

    return true;
}

bool settings_load_file(const char *path) {
    if (path == NULL) return false;

    struct vfs_node *node = NULL;
    if (!vfs_resolve_path(path, &node) || node == NULL) return false;

    char file_buf[1024];
    int64_t bytes_read = vfs_read(node, 0, sizeof(file_buf) - 1, file_buf);
    if (bytes_read <= 0) return false;

    file_buf[bytes_read] = '\0';
    return settings_deserialize(&active_settings, file_buf, (size_t)bytes_read);
}

bool settings_save_file(const char *path) {
    if (path == NULL) return false;

    char file_buf[1024];
    size_t len = settings_serialize(&active_settings, file_buf, sizeof(file_buf));
    if (len == 0) return false;

    struct vfs_node *node = NULL;
    if (!vfs_resolve_path(path, &node) || node == NULL) {
        if (vfs_create(path, VFS_FLAG_READ | VFS_FLAG_WRITE, &node) != 0 || node == NULL) return false;
    }

    int64_t written = vfs_write(node, 0, len, file_buf);
    return written == (int64_t)len;
}

/* GUI Window Implementation */
static const char *tab_names[TAB_COUNT] = {
    "Display", "Input", "Audio", "Network", "Time", "Appearance", "About"
};

void settings_window_switch_tab(enum settings_tab tab) {
    if (tab < TAB_COUNT) {
        current_tab = tab;
        settings_window_render();
    }
}

void settings_window_render(void) {
    if (settings_window_id < 0) return;

    /* Fill background */
    struct gfx_surface surf = {
        .buffer = settings_win_buffer,
        .width = 560,
        .height = 380,
        .pitch = 560 * sizeof(uint32_t)
    };
    gfx_fill_rect(&surf, 0, 0, 560, 380, GFX_COLOR_WINDOW_BG);

    /* Sidebar */
    gfx_fill_rect(&surf, 0, 0, 140, 380, GFX_COLOR_PANEL_DARK);
    gfx_draw_line(&surf, 140, 0, 140, 380, GFX_COLOR_GLASS_BORDER);

    /* Render Tab Buttons */
    for (int t = 0; t < TAB_COUNT; t++) {
        int y = 12 + t * 44;
        if ((enum settings_tab)t == current_tab) {
            gfx_fill_rect(&surf, 6, y, 128, 36, GFX_COLOR_EMERALD_MUTED);
            gfx_draw_rect(&surf, 6, y, 128, 36, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_string(&surf, 20, y + 10, tab_names[t], GFX_COLOR_TEXT_PRIMARY);
        } else {
            gfx_draw_string(&surf, 20, y + 10, tab_names[t], GFX_COLOR_TEXT_MUTED);
        }
    }

    /* Content Area */
    int cx = 160;
    int cy = 20;

    switch (current_tab) {
        case TAB_DISPLAY:
            gfx_draw_string(&surf, cx, cy, "Display & Monitor", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Resolution:  1024 x 768 @ 60Hz", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 55, "Color Depth: 32-bit TrueColor (ARGB8888)", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx, cy + 80, "Driver:      Limine GOP Linear Framebuffer", GFX_COLOR_TEXT_MUTED);

            /* Preset buttons */
            gfx_draw_rect(&surf, cx, cy + 120, 110, 30, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx + 12, cy + 128, "1024 x 768", GFX_COLOR_EMERALD_PRIMARY);

            gfx_draw_rect(&surf, cx + 125, cy + 120, 110, 30, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx + 137, cy + 128, "1280 x 720", GFX_COLOR_TEXT_MUTED);

            gfx_draw_rect(&surf, cx + 250, cy + 120, 110, 30, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx + 262, cy + 128, "1920x1080", GFX_COLOR_TEXT_MUTED);
            break;

        case TAB_INPUT:
            gfx_draw_string(&surf, cx, cy, "Input Devices", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Mouse Sensitivity: 5 / 10", GFX_COLOR_TEXT_PRIMARY);
            gfx_fill_rect(&surf, cx, cy + 55, 200, 10, GFX_COLOR_PANEL_DARK);
            gfx_fill_rect(&surf, cx, cy + 55, 100, 10, GFX_COLOR_EMERALD_PRIMARY);

            gfx_draw_string(&surf, cx, cy + 85, "Mouse Acceleration: Enabled", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 115, "Keyboard Repeat Delay: 250 ms", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 140, "Keyboard Repeat Rate:  30 cps", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 170, "Active Subsystem: PS/2 & USB HID", GFX_COLOR_TEXT_MUTED);
            break;

        case TAB_AUDIO:
            gfx_draw_string(&surf, cx, cy, "Sound & Audio Output", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Controller: Intel HD Audio (HDA)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 60, "Master Volume: 80%", GFX_COLOR_TEXT_PRIMARY);

            /* Volume slider */
            gfx_fill_rect(&surf, cx, cy + 85, 200, 12, GFX_COLOR_PANEL_DARK);
            gfx_fill_rect(&surf, cx, cy + 85, 160, 12, GFX_COLOR_EMERALD_PRIMARY);

            gfx_draw_string(&surf, cx, cy + 115, "Mute: Unmuted", GFX_COLOR_TEXT_PRIMARY);

            /* Play Chime Button */
            gfx_fill_rect(&surf, cx, cy + 150, 120, 32, GFX_COLOR_EMERALD_MUTED);
            gfx_draw_rect(&surf, cx, cy + 150, 120, 32, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_string(&surf, cx + 18, cy + 160, "Play Chime", GFX_COLOR_TEXT_PRIMARY);
            break;

        case TAB_NETWORK:
            gfx_draw_string(&surf, cx, cy, "Network Settings", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Active Device:  eth0 (Ethernet)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 55, "IP Address:     10.0.2.15", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 80, "Subnet Mask:    255.255.255.0", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 105, "Gateway:        10.0.2.2", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 130, "DNS Server:     8.8.8.8", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 160, "Configuration:  Static / Manual", GFX_COLOR_TEXT_MUTED);
            break;

        case TAB_TIME:
            gfx_draw_string(&surf, cx, cy, "Date & Time", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Clock Source: Hardware RTC (CMOS)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 60, "Timezone:     UTC+00:00 (Universal)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 90, "Format:       24-Hour Clock", GFX_COLOR_TEXT_PRIMARY);
            break;

        case TAB_APPEARANCE:
            gfx_draw_string(&surf, cx, cy, "Emerald Neo-Liquid Glass", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Accent Color: Emerald Primary (#00C853)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 60, "Glass Opacity: 80% Translucent", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 90, "Surface: Frosted Acrylic & Specular Sheen", GFX_COLOR_TEXT_MUTED);
            break;

        case TAB_ABOUT:
            gfx_draw_string(&surf, cx, cy, "About DAWN-OS", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "DAWN-OS 1.0 (Emerald Edition)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 55, "Architecture: x86_64 Bare-Metal", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 80, "Identity: Pure Custom Operating System", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 105, "Kernel: Non-Linux, Non-Windows Native", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 130, "Bootloader: Limine BIOS / UEFI Hybrid", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx, cy + 155, "Storage: NVMe + AHCI + FAT32 + ISO9660", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx, cy + 180, "Desktop: Neo-Liquid Glass Window Manager", GFX_COLOR_TEXT_MUTED);
            break;

        default:
            break;
    }
}

void settings_window_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x < 140) {
        int tab_index = (local_y - 12) / 44;
        if (tab_index >= 0 && tab_index < TAB_COUNT) {
            settings_window_switch_tab((enum settings_tab)tab_index);
        }
    } else {
        if (current_tab == TAB_AUDIO) {
            /* Check "Play Chime" button */
            if (local_x >= 160 && local_x <= 280 && local_y >= 170 && local_y <= 202) {
                audio_play_tone(880, 50); /* High chime */
            }
        }
    }
}

bool settings_window_open(void) {
    if (settings_window_id >= 0) {
        wm_focus_window((uint32_t)settings_window_id);
        return true;
    }

    struct window *win = wm_create_window("Settings", 120, 80, 560, 380, WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR);
    if (win == NULL) return false;

    win->client_buffer = settings_win_buffer;
    win->client_pitch = 560 * sizeof(uint32_t);
    settings_window_id = (int)win->id;

    current_tab = TAB_DISPLAY;
    settings_window_render();
    return true;
}

void settings_window_close(void) {
    if (settings_window_id >= 0) {
        wm_destroy_window((uint32_t)settings_window_id);
        settings_window_id = -1;
    }
}

/* Self-test */
bool settings_self_test(void) {
    if (!settings_initialize()) return false;

    /* Defaults verification */
    struct system_settings s = {0};
    settings_set_defaults(&s);
    if (s.screen_width != 1024 || s.screen_height != 768) return false;
    if (s.master_volume != 80 || s.audio_muted) return false;
    if (s.mouse_sensitivity != 5) return false;

    /* Serialization test */
    char buf[1024] = {0};
    size_t len = settings_serialize(&s, buf, sizeof(buf));
    if (len == 0) return false;

    /* Deserialization test */
    struct system_settings s2 = {0};
    if (!settings_deserialize(&s2, buf, len)) return false;
    if (s2.screen_width != 1024 || s2.screen_height != 768) return false;
    if (s2.master_volume != 80) return false;
    if (s2.mouse_sensitivity != 5) return false;

    /* Value modification & clamping */
    s.master_volume = 150; /* Out of range */
    s.mouse_sensitivity = 0;   /* Below minimum */
    settings_apply(&s);
    struct system_settings *active = settings_get_active();
    if (active->master_volume != 100) return false;
    if (active->mouse_sensitivity != 1) return false;

    /* Window lifecycle test */
    if (!settings_window_open()) return false;
    settings_window_switch_tab(TAB_AUDIO);
    if (current_tab != TAB_AUDIO) return false;
    settings_window_handle_click(30, 20); /* Click Display tab */
    if (current_tab != TAB_DISPLAY) return false;
    settings_window_close();

    return true;
}
