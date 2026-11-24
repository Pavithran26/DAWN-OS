/* SPDX-License-Identifier: Unlicense */
#include "settings.h"
#include "vfs.h"
#include "audio.h"
#include "net.h"
#include "window_manager.h"
#include "gfx.h"
#include "desktop.h"
#include "os_metadata.h"
#include "system_info.h"
#include "rtc.h"
#include "pci.h"
#include "device.h"

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
    "Display", "Input", "Audio", "Network", "Time", "Appearance", "About",
    "Credits", "Help", "Sys Info", "Hardware"
};

#define SETTINGS_TAB_STRIDE 28U
#define SETTINGS_TAB_HEIGHT 24U
#define SETTINGS_TAB_TOP 8U
#define SETTINGS_MAX_LINKS 12U

static void hex16_to_str(uint16_t val, char *buf) {
    const char hex_chars[] = "0123456789ABCDEF";
    buf[0] = hex_chars[(val >> 12) & 0x0FU];
    buf[1] = hex_chars[(val >> 8) & 0x0FU];
    buf[2] = hex_chars[(val >> 4) & 0x0FU];
    buf[3] = hex_chars[val & 0x0FU];
    buf[4] = '\0';
}

static const char *pci_class_to_name(const struct pci_device *dev) {
    if (dev == NULL) return "Unknown";
    if (dev->class_code == PCI_CLASS_MASS_STORAGE) {
        if (dev->subclass == PCI_SUBCLASS_STORAGE_SATA) return "Storage (AHCI)";
        if (dev->subclass == PCI_SUBCLASS_STORAGE_NVME) return "Storage (NVMe)";
        if (dev->subclass == PCI_SUBCLASS_STORAGE_IDE) return "Storage (IDE)";
        if (dev->vendor_id == 0x1AF4U && (dev->device_id == 0x1001U || dev->device_id == 0x1042U))
            return "Storage (VirtIO)";
        return "Mass Storage";
    }
    if (dev->class_code == PCI_CLASS_NETWORK) {
        if (dev->vendor_id == 0x1AF4U && (dev->device_id == 0x1000U || dev->device_id == 0x1041U))
            return "Net (VirtIO-Net)";
        if (dev->vendor_id == 0x8086U && dev->device_id == 0x100EU)
            return "Net (Intel e1000)";
        return "Network Adapter";
    }
    if (dev->class_code == PCI_CLASS_DISPLAY) {
        if (dev->vendor_id == 0x1234U && dev->device_id == 0x1111U)
            return "Display (QEMU VGA)";
        return "Display Adapter";
    }
    if (dev->class_code == PCI_CLASS_MULTIMEDIA) {
        if (dev->vendor_id == 0x8086U && dev->device_id == 0x2668U)
            return "Audio (Intel HDA)";
        return "Audio Controller";
    }
    if (dev->class_code == PCI_CLASS_BRIDGE) {
        if (dev->subclass == 0x00U) return "Bridge (Host PCI)";
        if (dev->subclass == 0x01U) return "Bridge (ISA)";
        return "PCI Bridge";
    }
    if (dev->class_code == PCI_CLASS_SERIAL_BUS) {
        if (dev->subclass == PCI_SUBCLASS_SERIAL_USB && dev->prog_if == PCI_PROGIF_USB_XHCI)
            return "USB 3.0 (xHCI)";
        return "Serial Bus / USB";
    }
    return "PCI Controller";
}

static const char *pci_driver_binding(const struct pci_device *dev) {
    if (dev == NULL) return "[Unbound]";
    if (dev->vendor_id == 0x1AF4U && (dev->device_id == 0x1001U || dev->device_id == 0x1042U))
        return "[Bound: virtio-blk]";
    if (dev->vendor_id == 0x1AF4U && (dev->device_id == 0x1000U || dev->device_id == 0x1041U))
        return "[Bound: virtio-net]";
    if (dev->class_code == PCI_CLASS_MASS_STORAGE && dev->subclass == PCI_SUBCLASS_STORAGE_SATA)
        return "[Bound: ahci]";
    if (dev->class_code == PCI_CLASS_MASS_STORAGE && dev->subclass == PCI_SUBCLASS_STORAGE_NVME)
        return "[Bound: nvme]";
    if (dev->class_code == PCI_CLASS_DISPLAY)
        return "[Bound: lfb-gop]";
    if (dev->class_code == PCI_CLASS_MULTIMEDIA)
        return "[Bound: hda]";
    if (dev->class_code == PCI_CLASS_BRIDGE)
        return "[System]";
    return "[Active]";
}

struct settings_link {
    int32_t x;
    int32_t y;
    int32_t width;
    const char *url;
};

static struct settings_link settings_links[SETTINGS_MAX_LINKS];
static size_t settings_link_count;
static uint32_t help_page;

static size_t settings_str_len(const char *text) {
    size_t length = 0U;
    while (text != NULL && text[length] != '\0') ++length;
    return length;
}

static void settings_draw_limited(struct gfx_surface *surf, uint32_t x, uint32_t y,
                                  const char *text, size_t max_chars, uint32_t color) {
    if (text == NULL) return;
    size_t length = settings_str_len(text);
    if (length > max_chars) length = max_chars;
    for (size_t index = 0U; index < length; ++index) {
        gfx_draw_char(surf, x + (uint32_t)(index * 8U), y, text[index], color);
    }
}

static size_t settings_format_labelled(char *dst, size_t capacity,
                                       const char *label, const char *value) {
    size_t position = 0U;
    if (dst == NULL || capacity == 0U) return 0U;
    label = (label != NULL) ? label : "";
    value = (value != NULL && value[0] != '\0') ? value : "Not available";
    for (size_t i = 0U; label[i] != '\0' && position + 1U < capacity; ++i) {
        dst[position++] = label[i];
    }
    for (size_t i = 0U; value[i] != '\0' && position + 1U < capacity; ++i) {
        dst[position++] = value[i];
    }
    dst[position] = '\0';
    return position;
}

static void settings_add_link(struct gfx_surface *surf, uint32_t x, uint32_t y,
                              const char *label, const char *url) {
    gfx_draw_string(surf, x, y, label, GFX_COLOR_EMERALD_ACCENT);
    if (!dawn_url_is_valid(url) || settings_link_count >= SETTINGS_MAX_LINKS) return;
    uint32_t width = (uint32_t)(settings_str_len(label) * 8U);
    gfx_draw_line(surf, (int32_t)x, (int32_t)(y + 14U), (int32_t)(x + width),
                  (int32_t)(y + 14U), GFX_COLOR_EMERALD_PRIMARY);
    settings_links[settings_link_count].x = (int32_t)x;
    settings_links[settings_link_count].y = (int32_t)y;
    settings_links[settings_link_count].width = (int32_t)width;
    settings_links[settings_link_count].url = url;
    ++settings_link_count;
}

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

    settings_link_count = 0U;

    /* Render Tab Buttons (compact stride so all ten tabs fit the client area) */
    for (uint32_t t = 0; t < TAB_COUNT; t++) {
        uint32_t y = SETTINGS_TAB_TOP + (t * SETTINGS_TAB_STRIDE);
        if ((enum settings_tab)t == current_tab) {
            gfx_fill_rect(&surf, 6U, y, 128U, SETTINGS_TAB_HEIGHT, GFX_COLOR_EMERALD_MUTED);
            gfx_draw_rect(&surf, 6U, y, 128U, SETTINGS_TAB_HEIGHT, GFX_COLOR_EMERALD_PRIMARY);
            gfx_draw_string(&surf, 20U, y + 4U, tab_names[t], GFX_COLOR_TEXT_PRIMARY);
        } else {
            gfx_draw_string(&surf, 20U, y + 4U, tab_names[t], GFX_COLOR_TEXT_MUTED);
        }
    }

    /* Content Area */
    uint32_t cx = 160U;
    uint32_t cy = 20U;

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

        case TAB_NETWORK: {
            gfx_draw_string(&surf, cx, cy, "Network Settings", GFX_COLOR_EMERALD_NEON);
            struct net_device *eth = net_get_default_device();
            if (eth != NULL) {
                char ip_buf[32] = {0};
                ip_to_str(eth->ip, ip_buf);
                char line_ip[64] = "IP Address:     ";
                for (size_t k = 0; ip_buf[k] != '\0' && 16 + k + 1 < sizeof(line_ip); k++) {
                    line_ip[16 + k] = ip_buf[k];
                }

                char mask_buf[32] = {0};
                ip_to_str(eth->netmask, mask_buf);
                char line_mask[64] = "Subnet Mask:    ";
                for (size_t k = 0; mask_buf[k] != '\0' && 16 + k + 1 < sizeof(line_mask); k++) {
                    line_mask[16 + k] = mask_buf[k];
                }

                char gw_buf[32] = {0};
                ip_to_str(eth->gateway, gw_buf);
                char line_gw[64] = "Gateway:        ";
                for (size_t k = 0; gw_buf[k] != '\0' && 16 + k + 1 < sizeof(line_gw); k++) {
                    line_gw[16 + k] = gw_buf[k];
                }

                char line_stats[64] = "Packets RX/TX:  ";
                size_t sp = 16;
                sp += u32_to_str((uint32_t)eth->rx_packets, line_stats + sp);
                line_stats[sp++] = ' ';
                line_stats[sp++] = '/';
                line_stats[sp++] = ' ';
                sp += u32_to_str((uint32_t)eth->tx_packets, line_stats + sp);
                line_stats[sp] = '\0';

                gfx_draw_string(&surf, cx, cy + 30, "Active Device:  eth0 (Ethernet UP)", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 55, line_ip, GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 80, line_mask, GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 105, line_gw, GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 130, line_stats, GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 160, "Link: UP (Full-Duplex 1000Mbps)", GFX_COLOR_EMERALD_ACCENT);
            }
            break;
        }

        case TAB_TIME: {
            gfx_draw_string(&surf, cx, cy, "Date & Time", GFX_COLOR_EMERALD_NEON);
            struct rtc_datetime sdt = {0};
            char stime_buf[36] = "Current Time: ";
            char sdate_buf[36] = "Current Date: ";
            if (rtc_get_datetime(&sdt)) {
                char s_t[16] = {0}, s_d[16] = {0};
                rtc_format_time_sec(&sdt, s_t, sizeof(s_t));
                rtc_format_date(&sdt, s_d, sizeof(s_d));
                for (size_t idx = 0; s_t[idx] != '\0' && idx < 12; idx++) stime_buf[14 + idx] = s_t[idx];
                for (size_t idx = 0; s_d[idx] != '\0' && idx < 12; idx++) sdate_buf[14 + idx] = s_d[idx];
            }
            gfx_draw_string(&surf, cx, cy + 28, stime_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 52, sdate_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 78, "Clock Source: Hardware RTC (CMOS)", GFX_COLOR_TEXT_SECONDARY);
            gfx_draw_string(&surf, cx, cy + 102, "Timezone:     UTC+00:00 (Universal)", GFX_COLOR_TEXT_SECONDARY);
            gfx_draw_string(&surf, cx, cy + 126, "Format:       24-Hour Clock", GFX_COLOR_TEXT_MUTED);
            break;
        }

        case TAB_APPEARANCE:
            gfx_draw_string(&surf, cx, cy, "Emerald Neo-Liquid Glass", GFX_COLOR_EMERALD_NEON);
            gfx_draw_string(&surf, cx, cy + 30, "Accent Color: Emerald Primary (#00C853)", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 60, "Glass Opacity: 80% Translucent", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, cy + 90, "Surface: Frosted Acrylic & Specular Sheen", GFX_COLOR_TEXT_MUTED);
            break;

        case TAB_ABOUT: {
            const struct dawn_metadata_view *meta = dawn_metadata_get();
            char line[80];
            uint32_t ay = cy - 8U;
            gfx_draw_icon(&surf, cx, ay, GFX_ICON_DAWN_LOGO, GFX_COLOR_EMERALD_ACCENT);
            gfx_draw_string(&surf, cx + 24U, ay, "About DAWN-OS", GFX_COLOR_EMERALD_NEON);
            if (meta == NULL) {
                gfx_draw_string(&surf, cx, ay + 30, "Metadata unavailable", GFX_COLOR_TEXT_PRIMARY);
                break;
            }
            settings_format_labelled(line, sizeof(line), "OS: ", meta->os_display_name);
            gfx_draw_string(&surf, cx, ay + 20, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Version: ", meta->version);
            gfx_draw_string(&surf, cx, ay + 38, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Channel: ", meta->channel);
            gfx_draw_string(&surf, cx, ay + 56, line, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Developer: ", meta->developer_display_name);
            gfx_draw_string(&surf, cx, ay + 74, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Project Owner: ", meta->developer_username);
            gfx_draw_string(&surf, cx, ay + 92, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Role: ", meta->developer_role);
            gfx_draw_string(&surf, cx, ay + 110, line, GFX_COLOR_TEXT_MUTED);
            settings_add_link(&surf, cx, ay + 130, "Official Website: https://pavithran26.net", meta->developer_website);
            settings_add_link(&surf, cx, ay + 150, "GitHub Profile: https://github.com/Pavithran26", meta->developer_github);
            settings_add_link(&surf, cx, ay + 170, "Repository: https://github.com/Pavithran26/DAWN-OS", meta->repository_url);
            settings_format_labelled(line, sizeof(line), "Kernel: ", meta->kernel_version);
            settings_draw_limited(&surf, cx, ay + 192, line, 48U, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Architecture: ", meta->architecture);
            gfx_draw_string(&surf, cx, ay + 210, line, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Build: ", meta->build_id);
            settings_draw_limited(&surf, cx, ay + 228, line, 48U, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Date: ", meta->build_date);
            gfx_draw_string(&surf, cx, ay + 246, line, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "License: ", meta->license_name);
            gfx_draw_string(&surf, cx, ay + 266, line, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, ay + 286, "Copyright (c) Pavithran.", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx, ay + 304, "See repository license for terms.", GFX_COLOR_TEXT_MUTED);
            break;
        }

        case TAB_CREDITS: {
            const struct dawn_metadata_view *meta = dawn_metadata_get();
            char line[80];
            gfx_draw_string(&surf, cx, cy, "Developer Profile & Credits", GFX_COLOR_EMERALD_NEON);
            if (meta == NULL) {
                gfx_draw_string(&surf, cx, cy + 30, "Metadata unavailable", GFX_COLOR_TEXT_PRIMARY);
                break;
            }
            gfx_draw_string(&surf, cx, cy + 28, "Developed and maintained by Pavithran", GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Role: ", meta->developer_role);
            gfx_draw_string(&surf, cx, cy + 52, line, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Project Owner: ", meta->developer_username);
            gfx_draw_string(&surf, cx, cy + 74, line, GFX_COLOR_TEXT_PRIMARY);
            settings_add_link(&surf, cx, cy + 100, "Official Website: https://pavithran26.net", meta->developer_website);
            settings_add_link(&surf, cx, cy + 124, "GitHub Profile: https://github.com/Pavithran26", meta->developer_github);
            settings_add_link(&surf, cx, cy + 148, "Repository: https://github.com/Pavithran26/DAWN-OS", meta->repository_url);
            settings_add_link(&surf, cx, cy + 172, "Issue Tracker: GitHub Issues", meta->issues_url);
            gfx_draw_string(&surf, cx, cy + 204, "Taskbar Footer:", GFX_COLOR_TEXT_MUTED);
            settings_draw_limited(&surf, cx, cy + 224, meta->footer_text, 48U, GFX_COLOR_TEXT_SECONDARY);
            settings_format_labelled(line, sizeof(line), "License: ", meta->license_name);
            gfx_draw_string(&surf, cx, cy + 248, line, GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx, cy + 270, "Copyright (c) Pavithran. See license terms.", GFX_COLOR_TEXT_MUTED);
            break;
        }

        case TAB_HELP_SUPPORT: {
            const struct dawn_metadata_view *meta = dawn_metadata_get();
            if (help_page > 3U) help_page = 0U;
            if (meta == NULL) {
                gfx_draw_string(&surf, cx, cy, "Metadata unavailable", GFX_COLOR_TEXT_PRIMARY);
                break;
            }
            if (help_page == 0U) {
                gfx_draw_string(&surf, cx, cy, "Help - 1. Getting Started", GFX_COLOR_EMERALD_NEON);
                gfx_draw_string(&surf, cx, cy + 28, "Desktop: Emerald Neo-Liquid Glass interface", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 50, "Start Menu: Click bottom-left logo for apps", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 72, "Terminal: Type 'help', 'sysinfo', 'ls', 'ps'", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 94, "Files: File Explorer browses VFS partitions", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 116, "Settings: Configure display, audio, network", GFX_COLOR_TEXT_PRIMARY);
                settings_add_link(&surf, cx, cy + 144, "Official Website: https://pavithran26.net", meta->support_website);
                settings_add_link(&surf, cx, cy + 168, "GitHub Repository: DAWN-OS", meta->repository_url);
            } else if (help_page == 1U) {
                gfx_draw_string(&surf, cx, cy, "Help - 2. Install & BIOS/UEFI Troubleshooting", GFX_COLOR_EMERALD_NEON);
                gfx_draw_string(&surf, cx, cy + 28, "Bootloader: Limine dual BIOS & UEFI support", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 50, "Storage: AHCI SATA & NVMe PCIe auto-detected", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 72, "Black Screen: Check GOP/VBE display modes", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 94, "Boot Freeze: Verify APIC & IOAPIC routing", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 116, "Recovery Shell: Boot with safe-mode flag", GFX_COLOR_TEXT_MUTED);
                settings_add_link(&surf, cx, cy + 144, "Bug Reports: GitHub Issues", meta->bug_report_url);
                settings_add_link(&surf, cx, cy + 168, "Documentation: pavithran26.net", meta->documentation_url ? meta->documentation_url : meta->support_website);
            } else if (help_page == 2U) {
                gfx_draw_string(&surf, cx, cy, "Help - 3. Applications & Subsystems", GFX_COLOR_EMERALD_NEON);
                gfx_draw_string(&surf, cx, cy + 28, "File Explorer: Copy, move, create dir, FAT32", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 50, "Terminal: Ring 3 ELF execution & pipes", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 72, "Task Manager: Process PID table & kill", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 94, "Storage Manager: MBR/GPT mount tables", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 116, "Audio: Intel HD Audio output & tones", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 138, "Dynamic Modules: Coming Soon", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 158, "GUI Browser: Coming Soon", GFX_COLOR_TEXT_MUTED);
                settings_add_link(&surf, cx, cy + 180, "Feature Requests: GitHub Issues", meta->feature_request_url);
            } else {
                gfx_draw_string(&surf, cx, cy, "Help - 4. FAQ & Known Issues", GFX_COLOR_EMERALD_NEON);
                gfx_draw_string(&surf, cx, cy + 28, "Q: Is DAWN-OS Linux or Windows based?", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 48, "A: No! Independent custom x86_64 kernel.", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 70, "Q: What filesystem is supported?", GFX_COLOR_TEXT_PRIMARY);
                gfx_draw_string(&surf, cx, cy + 90, "A: VFS with FAT32 read/write & ISO9660.", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 112, "Known Issue: DHCP client: Coming Soon", GFX_COLOR_TEXT_MUTED);
                gfx_draw_string(&surf, cx, cy + 132, "Known Issue: Web browser: Coming Soon", GFX_COLOR_TEXT_MUTED);
                settings_add_link(&surf, cx, cy + 156, "Bug Reports: GitHub Issues", meta->bug_report_url);
                settings_add_link(&surf, cx, cy + 180, "Feature Requests: GitHub Issues", meta->feature_request_url);
            }
            /* Prev / Next buttons and category indicator */
            gfx_draw_rect(&surf, cx, cy + 215, 70U, 24U, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx + 16U, cy + 221, "Prev", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_rect(&surf, cx + 80U, cy + 215, 70U, 24U, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx + 96U, cy + 221, "Next", GFX_COLOR_TEXT_PRIMARY);
            {
                char cat_str[24];
                cat_str[0] = 'P'; cat_str[1] = 'a'; cat_str[2] = 'g'; cat_str[3] = 'e';
                cat_str[4] = ' '; cat_str[5] = (char)('1' + help_page);
                cat_str[6] = ' '; cat_str[7] = 'o'; cat_str[8] = 'f';
                cat_str[9] = ' '; cat_str[10] = '4'; cat_str[11] = '\0';
                gfx_draw_string(&surf, cx + 165U, cy + 221, cat_str, GFX_COLOR_TEXT_MUTED);
            }
            break;
        }

        case TAB_SYSTEM_INFO: {
            const struct dawn_metadata_view *meta = dawn_metadata_get();
            struct dawn_system_info info;
            char line[80];
            gfx_draw_string(&surf, cx, cy, "System Information", GFX_COLOR_EMERALD_NEON);
            if (meta == NULL) {
                gfx_draw_string(&surf, cx, cy + 30, "Metadata unavailable", GFX_COLOR_TEXT_PRIMARY);
                break;
            }
            settings_format_labelled(line, sizeof(line), "OS: ", meta->os_display_name);
            gfx_draw_string(&surf, cx, cy + 22, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Version: ", meta->version);
            gfx_draw_string(&surf, cx, cy + 42, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Kernel: ", meta->kernel_version);
            settings_draw_limited(&surf, cx, cy + 62, line, 50U, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Architecture: ", meta->architecture);
            gfx_draw_string(&surf, cx, cy + 82, line, GFX_COLOR_TEXT_PRIMARY);
            settings_format_labelled(line, sizeof(line), "Build: ", meta->build_id);
            settings_draw_limited(&surf, cx, cy + 102, line, 50U, GFX_COLOR_TEXT_MUTED);
            settings_format_labelled(line, sizeof(line), "Build Date: ", meta->build_date);
            gfx_draw_string(&surf, cx, cy + 122, line, GFX_COLOR_TEXT_MUTED);
            if (dawn_system_info_refresh(&info)) {
                char memory[24];
                char uptime[24];
                settings_format_labelled(line, sizeof(line), "CPU: ", info.cpu_model);
                settings_draw_limited(&surf, cx, cy + 144, line, 50U, GFX_COLOR_TEXT_PRIMARY);
                {
                    char cores_buf[32];
                    char c_on[12];
                    (void)dawn_decimal_string(info.online_cpu_count, c_on, sizeof(c_on));
                    size_t cp = 0;
                    const char *pfx = (info.online_cpu_count > 1) ? " (SMP Active)" : " (Single Core)";
                    for (size_t k = 0; c_on[k] != '\0' && cp + 1 < sizeof(cores_buf); k++) cores_buf[cp++] = c_on[k];
                    for (size_t k = 0; pfx[k] != '\0' && cp + 1 < sizeof(cores_buf); k++) cores_buf[cp++] = pfx[k];
                    cores_buf[cp] = '\0';
                    settings_format_labelled(line, sizeof(line), "CPU Cores: ", cores_buf);
                    gfx_draw_string(&surf, cx, cy + 164, line, GFX_COLOR_TEXT_PRIMARY);
                }
                if (dawn_human_bytes(info.physical_memory_bytes, memory, sizeof(memory)) != 0U) {
                    settings_format_labelled(line, sizeof(line), "Total Memory: ", memory);
                    gfx_draw_string(&surf, cx, cy + 184, line, GFX_COLOR_TEXT_MUTED);
                }
                if (dawn_human_bytes(info.free_memory_bytes, memory, sizeof(memory)) != 0U) {
                    settings_format_labelled(line, sizeof(line), "Free Memory: ", memory);
                    gfx_draw_string(&surf, cx, cy + 204, line, GFX_COLOR_TEXT_MUTED);
                }
                settings_format_labelled(line, sizeof(line), "Boot Mode: ", info.boot_mode);
                gfx_draw_string(&surf, cx, cy + 224, line, GFX_COLOR_TEXT_MUTED);
                if (dawn_decimal_string(info.uptime_seconds, uptime, sizeof(uptime)) != 0U) {
                    settings_format_labelled(line, sizeof(line), "Uptime (sec): ", uptime);
                    gfx_draw_string(&surf, cx, cy + 244, line, GFX_COLOR_TEXT_MUTED);
                }
                settings_format_labelled(line, sizeof(line), "Storage: ", info.storage_summary);
                settings_draw_limited(&surf, cx, cy + 264, line, 50U, GFX_COLOR_TEXT_MUTED);
                settings_format_labelled(line, sizeof(line), "Filesystem: ", info.filesystem_summary);
                gfx_draw_string(&surf, cx, cy + 284, line, GFX_COLOR_TEXT_MUTED);
            } else {
                gfx_draw_string(&surf, cx, cy + 144, "System information unavailable", GFX_COLOR_TEXT_MUTED);
            }
            settings_add_link(&surf, cx, cy + 304, "Report a Problem: GitHub Issues", meta->bug_report_url);
            settings_add_link(&surf, cx, cy + 322, "Hardware Inspector: PCI Devices", "tab:hardware");
            break;
        }

        case TAB_HARDWARE: {
            gfx_draw_string(&surf, cx, cy, "Hardware Inspector — PCI & Controllers", GFX_COLOR_EMERALD_NEON);

            size_t pci_count = pci_get_device_count();
            char sub_buf[64];
            char c_pci[16];
            u32_to_str((uint32_t)pci_count, c_pci);
            size_t sub_p = 0U;
            const char *sp1 = "Discovered: ";
            for (size_t k = 0U; sp1[k] != '\0' && sub_p + 1U < sizeof(sub_buf); k++) sub_buf[sub_p++] = sp1[k];
            for (size_t k = 0U; c_pci[k] != '\0' && sub_p + 1U < sizeof(sub_buf); k++) sub_buf[sub_p++] = c_pci[k];
            const char *sp2 = " PCI hardware devices | Core buses mapped";
            for (size_t k = 0U; sp2[k] != '\0' && sub_p + 1U < sizeof(sub_buf); k++) sub_buf[sub_p++] = sp2[k];
            sub_buf[sub_p] = '\0';
            gfx_draw_string(&surf, cx, cy + 20, sub_buf, GFX_COLOR_TEXT_MUTED);

            /* Table Header */
            gfx_fill_rect(&surf, cx, cy + 38, 388, 18, GFX_COLOR_PANEL_DARK);
            gfx_draw_string(&surf, cx + 4, cy + 42, "#", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx + 24, cy + 42, "B:D.F", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx + 80, cy + 42, "VENDOR:DEV", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx + 180, cy + 42, "CLASS / TYPE", GFX_COLOR_TEXT_MUTED);
            gfx_draw_string(&surf, cx + 295, cy + 42, "DRIVER", GFX_COLOR_TEXT_MUTED);

            /* Rows (up to 7 items) */
            uint32_t row_y = cy + 58;
            for (size_t i = 0U; i < pci_count && i < 7U; i++) {
                const struct pci_device *pdev = pci_get_device(i);
                if (pdev == NULL) continue;

                /* Alternating row tint */
                if ((i & 1U) != 0U) {
                    gfx_fill_rect(&surf, cx, row_y - 2, 388, 20, 0x300A251BU);
                }

                char num_str[8];
                u32_to_str((uint32_t)(i + 1U), num_str);
                gfx_draw_string(&surf, cx + 4, row_y, num_str, GFX_COLOR_TEXT_PRIMARY);

                char bdf_str[16];
                char b_s[8], d_s[8], f_s[8];
                u32_to_str((uint32_t)pdev->bus, b_s);
                u32_to_str((uint32_t)pdev->slot, d_s);
                u32_to_str((uint32_t)pdev->func, f_s);
                size_t bp = 0U;
                if (pdev->bus < 10U) bdf_str[bp++] = '0';
                for (size_t k = 0U; b_s[k] != '\0' && bp + 1U < sizeof(bdf_str); k++) bdf_str[bp++] = b_s[k];
                bdf_str[bp++] = ':';
                if (pdev->slot < 10U) bdf_str[bp++] = '0';
                for (size_t k = 0U; d_s[k] != '\0' && bp + 1U < sizeof(bdf_str); k++) bdf_str[bp++] = d_s[k];
                bdf_str[bp++] = '.';
                for (size_t k = 0U; f_s[k] != '\0' && bp + 1U < sizeof(bdf_str); k++) bdf_str[bp++] = f_s[k];
                bdf_str[bp] = '\0';
                gfx_draw_string(&surf, cx + 24, row_y, bdf_str, GFX_COLOR_TEXT_PRIMARY);

                char vid_str[6], did_str[6];
                hex16_to_str(pdev->vendor_id, vid_str);
                hex16_to_str(pdev->device_id, did_str);
                char ven_dev[16];
                size_t vp = 0U;
                for (size_t k = 0U; vid_str[k] != '\0' && vp + 1U < sizeof(ven_dev); k++) ven_dev[vp++] = vid_str[k];
                ven_dev[vp++] = ':';
                for (size_t k = 0U; did_str[k] != '\0' && vp + 1U < sizeof(ven_dev); k++) ven_dev[vp++] = did_str[k];
                ven_dev[vp] = '\0';
                gfx_draw_string(&surf, cx + 80, row_y, ven_dev, GFX_COLOR_TEXT_PRIMARY);

                const char *cname = pci_class_to_name(pdev);
                gfx_draw_string(&surf, cx + 180, row_y, cname, GFX_COLOR_TEXT_SECONDARY);

                const char *dbind = pci_driver_binding(pdev);
                uint32_t bind_col = (dbind[1] == 'B') ? GFX_COLOR_EMERALD_ACCENT : GFX_COLOR_TEXT_MUTED;
                gfx_draw_string(&surf, cx + 295, row_y, dbind, bind_col);

                row_y += 20;
            }

            /* Core System Peripherals section */
            gfx_draw_line(&surf, cx, row_y + 4, cx + 388, row_y + 4, GFX_COLOR_GLASS_BORDER);
            gfx_draw_string(&surf, cx, row_y + 10, "Platform & Core System Peripherals:", GFX_COLOR_EMERALD_ACCENT);
            gfx_draw_string(&surf, cx, row_y + 28, "Keyboard & Mouse: PS/2 Dual Channel Controller", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, row_y + 46, "Timers & Clocks:  Local APIC, IOAPIC & CMOS RTC", GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, cx, row_y + 64, "ACPI & Power:     MADT Processor & I/O APIC Tables", GFX_COLOR_TEXT_MUTED);

            break;
        }

        default:
            break;
    }
}

void settings_window_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x >= 0 && local_x < 140 &&
        local_y >= (int32_t)SETTINGS_TAB_TOP &&
        local_y < (int32_t)(SETTINGS_TAB_TOP + ((uint32_t)TAB_COUNT * SETTINGS_TAB_STRIDE))) {
        int tab_index = (local_y - (int32_t)SETTINGS_TAB_TOP) / (int32_t)SETTINGS_TAB_STRIDE;
        if (tab_index >= 0 && tab_index < (int32_t)TAB_COUNT) {
            settings_window_switch_tab((enum settings_tab)tab_index);
        }
        return;
    }

    for (size_t index = 0U; index < settings_link_count; ++index) {
        const struct settings_link *link = &settings_links[index];
        if (local_x >= link->x && local_x <= link->x + link->width &&
            local_y >= link->y && local_y <= link->y + 14) {
            if (str_equal(link->url, "tab:hardware")) {
                settings_window_switch_tab(TAB_HARDWARE);
                return;
            }
            (void)desktop_open_url(link->url);
            return;
        }
    }

    if (current_tab == TAB_AUDIO) {
        /* Check "Play Chime" button */
        if (local_x >= 160 && local_x <= 280 && local_y >= 170 && local_y <= 202) {
            audio_play_tone(880, 50); /* High chime */
        }
    } else if (current_tab == TAB_HELP_SUPPORT) {
        /* Help navigation buttons are at content cy (20) + 215 = 235..259 */
        if (local_y >= 235 && local_y <= 259) {
            if (local_x >= 160 && local_x <= 230) {
                help_page = (help_page == 0U) ? 3U : (help_page - 1U);
                settings_window_render();
                return;
            }
            if (local_x >= 240 && local_x <= 310) {
                help_page = (help_page + 1U) % 4U;
                settings_window_render();
                return;
            }
        }
    }
}

static void settings_on_window_close(struct window *win) {
    (void)win;
    settings_window_id = -1;
}

int settings_window_get_id(void) {
    return settings_window_id;
}

bool settings_window_open_tab(enum settings_tab tab) {
    if (tab >= TAB_COUNT) return false;
    if (settings_window_id >= 0) {
        if (wm_get_window_by_id((uint32_t)settings_window_id) != NULL) {
            wm_focus_window((uint32_t)settings_window_id);
            settings_window_switch_tab(tab);
            return true;
        }
        settings_window_id = -1;
    }
    if (!settings_window_open()) return false;
    settings_window_switch_tab(tab);
    return true;
}

bool settings_window_open(void) {
    if (settings_window_id >= 0) {
        if (wm_get_window_by_id((uint32_t)settings_window_id) != NULL) {
            wm_focus_window((uint32_t)settings_window_id);
            return true;
        }
        settings_window_id = -1;
    }

    struct window *win = wm_create_window("Settings", 120, 80, 560, 380, WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR);
    if (win == NULL) return false;

    win->client_buffer = settings_win_buffer;
    win->client_pitch = 560 * sizeof(uint32_t);
    win->on_click = settings_window_handle_click;
    win->on_close = settings_on_window_close;
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
    if (settings_window_get_id() < 0) return false;
    settings_window_switch_tab(TAB_AUDIO);
    if (current_tab != TAB_AUDIO) return false;
    settings_window_handle_click(30, 20); /* Click Display tab */
    if (current_tab != TAB_DISPLAY) return false;

    if (!settings_window_open_tab(TAB_ABOUT)) return false;
    if (current_tab != TAB_ABOUT || settings_link_count == 0U) return false;
    settings_window_switch_tab(TAB_CREDITS);
    if (current_tab != TAB_CREDITS || settings_link_count == 0U) return false;
    settings_window_switch_tab(TAB_HELP_SUPPORT);
    settings_window_handle_click(270, 240); /* Next help page */
    if (current_tab != TAB_HELP_SUPPORT) return false;
    settings_window_switch_tab(TAB_SYSTEM_INFO);
    if (current_tab != TAB_SYSTEM_INFO) return false;
    settings_window_switch_tab(TAB_HARDWARE);
    if (current_tab != TAB_HARDWARE) return false;
    if (pci_get_device_count() == 0U) return false;
    settings_window_close();
    if (settings_window_get_id() >= 0) return false;

    return true;
}
