/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

size_t dawn_strlen(const char *str) {
    size_t len = 0;
    if (str == NULL) return 0;
    while (str[len] != '\0') ++len;
    return len;
}

int dawn_strcmp(const char *s1, const char *s2) {
    if (s1 == NULL || s2 == NULL) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void *dawn_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *dawn_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

void dawn_print(const char *str) {
    if (str == NULL) return;
    size_t len = dawn_strlen(str);
    if (len > 0) {
        dawn_write(DAWN_STDOUT_FILENO, str, len);
    }
}

void dawn_println(const char *str) {
    dawn_print(str);
    dawn_print("\n");
}

void dawn_print_hex(uint64_t val) {
    static const char hex_digits[] = "0123456789abcdef";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        buf[2 + i] = hex_digits[(val >> shift) & 0x0fULL];
    }
    buf[18] = '\0';
    dawn_print(buf);
}

void dawn_print_dec(uint64_t val) {
    char buf[24];
    int idx = 0;
    if (val == 0) {
        dawn_print("0");
        return;
    }
    char rev[24];
    while (val > 0) {
        rev[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < idx; i++) {
        buf[i] = rev[idx - 1 - i];
    }
    buf[idx] = '\0';
    dawn_print(buf);
}
