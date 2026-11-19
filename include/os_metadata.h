/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_OS_METADATA_H
#define DAWN_OS_METADATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Read-only runtime view of the authoritative config/os_metadata.json file.
 * All strings point into the generated header and must not be modified.
 * Invalid metadata is rejected at initialization; no value is ever replaced
 * with invented data.
 */
struct dawn_metadata_view {
    const char *os_name;
    const char *os_display_name;
    const char *os_description;
    const char *architecture;
    const char *version;
    const char *channel;
    const char *build_id;
    const char *build_date;
    const char *kernel_version;

    const char *developer_name;
    const char *developer_display_name;
    const char *developer_username;
    const char *developer_role;
    const char *developer_website;
    const char *developer_github;

    const char *project_name;
    const char *repository_url;
    const char *issues_url;
    const char *documentation_url;

    const char *support_website;
    const char *bug_report_url;
    const char *feature_request_url;

    const char *logo_asset;
    const char *primary_color;
    const char *design_language;

    const char *copyright_holder;
    const char *copyright_notice;
    const char *copyright_year;

    /* Built from the metadata above; never hardcoded. */
    const char *footer_text;
    const char *footer_domain;
    const char *credits_line;
    const char *not_available;
};

#define DAWN_META_URL_MAX 256U
#define DAWN_META_FOOTER_MAX 160U

bool dawn_metadata_initialize(void);
const struct dawn_metadata_view *dawn_metadata_get(void);
bool dawn_metadata_is_valid(void);

/* Returns the validated value or "Not available" for absent optional fields. */
const char *dawn_metadata_or_unavailable(const char *value);

/* Strict http/https validation used before any link is opened or rendered. */
bool dawn_url_is_valid(const char *url);

/* Escapes control/HTML-significant characters into dst. Returns bytes written
 * excluding the terminator, or 0 when the buffer is too small. */
size_t dawn_escape_text(const char *src, char *dst, size_t capacity);

/* Copies a validated URL into dst. Returns false for invalid or oversized URLs. */
bool dawn_url_copy(const char *url, char *dst, size_t capacity);

#ifdef DAWN_KERNEL_TEST
bool os_metadata_self_test(void);
#endif

#endif /* DAWN_OS_METADATA_H */
