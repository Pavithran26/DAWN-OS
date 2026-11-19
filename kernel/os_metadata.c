/* SPDX-License-Identifier: Unlicense */
#include "os_metadata.h"
#include "dawn_metadata_generated.h"

static const char unavailable[] = "Not available";

static struct dawn_metadata_view view;
static char footer_text[DAWN_META_FOOTER_MAX];
static char footer_domain[DAWN_META_URL_MAX];
static char credits_line[DAWN_META_FOOTER_MAX];
static bool initialized;
static bool valid;

static size_t local_str_len(const char *text) {
    size_t length = 0U;
    if (text == NULL) return 0U;
    while (text[length] != '\0') ++length;
    return length;
}

static bool local_semver_valid(const char *text) {
    /* major.minor.patch with an optional pre-release suffix. */
    const char *cursor = text;
    if (text == NULL) return false;
    for (unsigned int part = 0U; part < 3U; ++part) {
        const char *start = cursor;
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        if (cursor == start) return false;
        if (part < 2U) {
            if (*cursor != '.') return false;
            ++cursor;
        }
    }
    if (*cursor == '\0') return true;
    if (*cursor != '-') return false;
    ++cursor;
    if (*cursor == '\0') return false;
    while (*cursor != '\0') {
        char ch = *cursor;
        bool allowed = (ch >= '0' && ch <= '9') ||
                       (ch >= 'a' && ch <= 'z') ||
                       (ch >= 'A' && ch <= 'Z') ||
                       ch == '.' || ch == '-';
        if (!allowed) return false;
        ++cursor;
    }
    return true;
}

bool dawn_url_is_valid(const char *url) {
    const char *host = NULL;
    size_t length = 0U;
    if (url == NULL) return false;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
        url[4] == ':' && url[5] == '/' && url[6] == '/') {
        host = url + 7;
    } else if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
               url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/') {
        host = url + 8;
    } else {
        return false;
    }
    if (host == NULL || *host == '\0') return false;
    while (host[length] != '\0') {
        unsigned char ch = (unsigned char)host[length];
        if (ch <= 0x20U || ch == 0x7fU) return false;
        ++length;
    }
    return length > 0U && length < DAWN_META_URL_MAX;
}

const char *dawn_metadata_or_unavailable(const char *value) {
    if (value == NULL || value[0] == '\0') return unavailable;
    return value;
}

size_t dawn_escape_text(const char *src, char *dst, size_t capacity) {
    size_t written = 0U;
    size_t index = 0U;
    if (src == NULL || dst == NULL || capacity == 0U) return 0U;
    while (src[index] != '\0') {
        unsigned char ch = (unsigned char)src[index];
        const char *replacement = NULL;
        size_t needed = 1U;
        switch (ch) {
            case '<': replacement = "&lt;"; needed = 4U; break;
            case '>': replacement = "&gt;"; needed = 4U; break;
            case '&': replacement = "&amp;"; needed = 5U; break;
            case '"': replacement = "&quot;"; needed = 6U; break;
            case '\'': replacement = "&#39;"; needed = 5U; break;
            default: break;
        }
        if (written + needed + 1U > capacity) return 0U;
        if (replacement != NULL) {
            for (size_t i = 0U; i < needed; ++i) dst[written + i] = replacement[i];
            written += needed;
        } else {
            dst[written++] = (char)ch;
        }
        ++index;
    }
    dst[written] = '\0';
    return written;
}

bool dawn_url_copy(const char *url, char *dst, size_t capacity) {
    size_t index = 0U;
    if (!dawn_url_is_valid(url) || dst == NULL || capacity == 0U) return false;
    while (url[index] != '\0') {
        if (index + 1U >= capacity) return false;
        dst[index] = url[index];
        ++index;
    }
    dst[index] = '\0';
    return true;
}

static bool local_copy(char *dst, size_t capacity, const char *a, const char *sep,
                       const char *b, const char *sep2, const char *c) {
    size_t written = 0U;
    const char *parts[3] = {a, b, c};
    const char *separators[2] = {sep, sep2};
    for (size_t part = 0U; part < 3U; ++part) {
        if (part > 0U) {
            const char *separator = separators[part - 1U];
            size_t separator_length = local_str_len(separator);
            if (written + separator_length + 1U > capacity) return false;
            for (size_t i = 0U; i < separator_length; ++i) dst[written++] = separator[i];
        }
        const char *text = parts[part];
        size_t length = local_str_len(text);
        if (written + length + 1U > capacity) return false;
        for (size_t i = 0U; i < length; ++i) dst[written++] = text[i];
    }
    dst[written] = '\0';
    return true;
}

bool dawn_metadata_initialize(void) {
    if (initialized) return valid;

    if (DAWN_META_SCHEMA_VERSION != 1) return false;
    if (DAWN_META_OS_NAME[0] == '\0' || DAWN_META_DEVELOPER_NAME[0] == '\0' ||
        DAWN_META_PROJECT_NAME[0] == '\0') return false;
    if (!local_semver_valid(DAWN_META_OS_VERSION)) return false;
    if (DAWN_META_OS_ARCHITECTURE[0] != 'x' || DAWN_META_OS_ARCHITECTURE[1] != '8' ||
        DAWN_META_OS_ARCHITECTURE[2] != '6' || DAWN_META_OS_ARCHITECTURE[3] != '4' ||
        DAWN_META_OS_ARCHITECTURE[4] != '\0') return false;
    if (!dawn_url_is_valid(DAWN_META_DEVELOPER_WEBSITE) ||
        !dawn_url_is_valid(DAWN_META_DEVELOPER_GITHUB) ||
        !dawn_url_is_valid(DAWN_META_REPOSITORY_URL) ||
        !dawn_url_is_valid(DAWN_META_ISSUES_URL) ||
        !dawn_url_is_valid(DAWN_META_SUPPORT_WEBSITE) ||
        !dawn_url_is_valid(DAWN_META_BUG_REPORT_URL) ||
        !dawn_url_is_valid(DAWN_META_FEATURE_REQUEST_URL)) return false;
    if (DAWN_META_PRIMARY_COLOR[0] == '\0' ||
        DAWN_META_DESIGN_LANGUAGE[0] == '\0' ||
        DAWN_META_LOGO_ASSET[0] == '\0') return false;

    view.os_name = DAWN_META_OS_NAME;
    view.os_display_name = DAWN_META_OS_DISPLAY_NAME;
    view.os_description = DAWN_META_OS_DESCRIPTION;
    view.architecture = DAWN_META_OS_ARCHITECTURE;
    view.version = DAWN_META_OS_VERSION;
    view.channel = DAWN_META_OS_CHANNEL;
    view.build_id = DAWN_META_BUILD_ID;
    view.build_date = DAWN_META_BUILD_DATE;
    view.kernel_version = DAWN_META_KERNEL_VERSION;
    view.developer_name = DAWN_META_DEVELOPER_NAME;
    view.developer_display_name = DAWN_META_DEVELOPER_DISPLAY_NAME;
    view.developer_username = DAWN_META_DEVELOPER_USERNAME;
    view.developer_role = DAWN_META_DEVELOPER_ROLE;
    view.developer_website = DAWN_META_DEVELOPER_WEBSITE;
    view.developer_github = DAWN_META_DEVELOPER_GITHUB;
    view.project_name = DAWN_META_PROJECT_NAME;
    view.repository_url = DAWN_META_REPOSITORY_URL;
    view.issues_url = DAWN_META_ISSUES_URL;
#if DAWN_META_HAS_LICENSE
    view.license_name = DAWN_META_LICENSE;
#else
    view.license_name = NULL;
#endif
#if DAWN_META_HAS_PROJECT_DOC_URL
    view.documentation_url = DAWN_META_PROJECT_DOC_URL;
#else
    view.documentation_url = NULL;
#endif
    view.support_website = DAWN_META_SUPPORT_WEBSITE;
    view.bug_report_url = DAWN_META_BUG_REPORT_URL;
    view.feature_request_url = DAWN_META_FEATURE_REQUEST_URL;
    view.logo_asset = DAWN_META_LOGO_ASSET;
    view.primary_color = DAWN_META_PRIMARY_COLOR;
    view.design_language = DAWN_META_DESIGN_LANGUAGE;
    view.copyright_holder = DAWN_META_COPYRIGHT_HOLDER;
    view.copyright_notice = DAWN_META_COPYRIGHT_NOTICE;
#if DAWN_META_HAS_COPYRIGHT_YEAR
    view.copyright_year = DAWN_META_COPYRIGHT_YEAR;
#else
    view.copyright_year = NULL;
#endif
    view.not_available = unavailable;

    if (!dawn_url_copy(DAWN_META_DEVELOPER_WEBSITE, footer_domain, sizeof(footer_domain))) {
        initialized = true;
        valid = false;
        return false;
    }
    /* Strip the scheme so the footer shows just the domain. */
    {
        char *scheme_end = footer_domain;
        while (*scheme_end != '\0' && *scheme_end != '/') ++scheme_end;
        if (scheme_end[0] == '/' && scheme_end[1] == '/') {
            size_t index = 0U;
            const char *domain = scheme_end + 2;
            while (domain[index] != '\0') {
                footer_domain[index] = domain[index];
                ++index;
            }
            footer_domain[index] = '\0';
        }
    }

    if (!local_copy(footer_text, sizeof(footer_text), DAWN_META_OS_DISPLAY_NAME,
                    " | Developed by ", DAWN_META_DEVELOPER_DISPLAY_NAME,
                    " | ", footer_domain)) {
        initialized = true;
        valid = false;
        return false;
    }
    if (!local_copy(credits_line, sizeof(credits_line), DAWN_META_DEVELOPER_DISPLAY_NAME,
                    " - ", DAWN_META_DEVELOPER_ROLE, " - ", DAWN_META_DEVELOPER_USERNAME)) {
        initialized = true;
        valid = false;
        return false;
    }

    view.footer_text = footer_text;
    view.footer_domain = footer_domain;
    view.credits_line = credits_line;

    initialized = true;
    valid = true;
    return true;
}

const struct dawn_metadata_view *dawn_metadata_get(void) {
    if (!dawn_metadata_initialize()) return NULL;
    return &view;
}

bool dawn_metadata_is_valid(void) {
    return dawn_metadata_initialize();
}

bool os_metadata_self_test(void) {
    const struct dawn_metadata_view *meta = NULL;

    if (!dawn_metadata_initialize()) return false;
    meta = dawn_metadata_get();
    if (meta == NULL) return false;

    if (meta->license_name == NULL || local_str_len(meta->license_name) == 0U) return false;

    if (!local_semver_valid("0.1.0")) return false;
    if (local_semver_valid("0.1")) return false;
    if (local_semver_valid("v0.1.0")) return false;
    if (local_semver_valid("0.1.0.4")) return false;
    if (local_semver_valid("")) return false;

    if (!dawn_url_is_valid("https://pavithran26.net")) return false;
    if (!dawn_url_is_valid("http://example.com/path")) return false;
    if (dawn_url_is_valid("javascript:alert(1)")) return false;
    if (dawn_url_is_valid("ftp://example.com")) return false;
    if (dawn_url_is_valid("https://")) return false;
    if (dawn_url_is_valid(NULL)) return false;
    if (dawn_url_is_valid("https://example.com/a b")) return false;

    if (meta->documentation_url != NULL && !dawn_url_is_valid(meta->documentation_url)) return false;
    if (dawn_metadata_or_unavailable(meta->documentation_url) == NULL) return false;
    if (local_str_len(unavailable) == 0U) return false;

    {
        char escaped[32];
        if (dawn_escape_text("<a&\"'", escaped, sizeof(escaped)) == 0U) return false;
        if (escaped[0] != '&' || escaped[1] != 'l') return false;
        if (dawn_escape_text("safe", escaped, 5U) != 4U) return false;
        if (dawn_escape_text("safe", escaped, 4U) != 0U) return false;
    }

    {
        char copied[DAWN_META_URL_MAX];
        if (!dawn_url_copy(meta->developer_website, copied, sizeof(copied))) return false;
        if (!dawn_url_is_valid(copied)) return false;
        if (dawn_url_copy("javascript:alert(1)", copied, sizeof(copied))) return false;
    }

    if (meta->footer_text == NULL || meta->footer_domain == NULL ||
        meta->credits_line == NULL) return false;
    if (local_str_len(meta->footer_domain) == 0U) return false;
    if (local_str_len(meta->footer_text) < local_str_len(meta->os_display_name)) return false;

    return true;
}
