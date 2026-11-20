/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

/*
 * Dynamic Memory Allocator (Free-list built on dawn_sbrk)
 */
struct malloc_header {
    size_t size;
    bool is_free;
    struct malloc_header *next;
};

#define MALLOC_HEADER_SIZE sizeof(struct malloc_header)
static struct malloc_header *malloc_head = NULL;

void *dawn_malloc(size_t size) {
    if (size == 0) return NULL;

    /* Align size to 16 bytes */
    size = (size + 15) & ~15ULL;

    /* Search for available free block */
    struct malloc_header *curr = malloc_head;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            curr->is_free = false;
            return (void *)((uint8_t *)curr + MALLOC_HEADER_SIZE);
        }
        curr = curr->next;
    }

    /* Expand heap */
    size_t total_needed = size + MALLOC_HEADER_SIZE;
    /* Request at least 4KB to avoid tiny sbrk calls */
    size_t alloc_chunk = (total_needed + 4095) & ~4095ULL;
    void *raw = dawn_sbrk((intptr_t)alloc_chunk);
    if (raw == (void *)-1 || raw == NULL) {
        return NULL;
    }

    struct malloc_header *new_header = (struct malloc_header *)raw;
    new_header->size = alloc_chunk - MALLOC_HEADER_SIZE;
    new_header->is_free = false;
    new_header->next = NULL;

    if (malloc_head == NULL) {
        malloc_head = new_header;
    } else {
        struct malloc_header *tail = malloc_head;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = new_header;
    }

    return (void *)((uint8_t *)new_header + MALLOC_HEADER_SIZE);
}

void dawn_free(void *ptr) {
    if (ptr == NULL) return;
    struct malloc_header *header = (struct malloc_header *)((uint8_t *)ptr - MALLOC_HEADER_SIZE);
    header->is_free = true;

    /* Coalesce consecutive free blocks */
    struct malloc_header *curr = malloc_head;
    while (curr != NULL && curr->next != NULL) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += curr->next->size + MALLOC_HEADER_SIZE;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void *dawn_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = dawn_malloc(total);
    if (ptr != NULL) {
        dawn_memset(ptr, 0, total);
    }
    return ptr;
}

void *dawn_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return dawn_malloc(size);
    if (size == 0) {
        dawn_free(ptr);
        return NULL;
    }
    struct malloc_header *header = (struct malloc_header *)((uint8_t *)ptr - MALLOC_HEADER_SIZE);
    if (header->size >= size) return ptr;

    void *new_ptr = dawn_malloc(size);
    if (new_ptr != NULL) {
        dawn_memcpy(new_ptr, ptr, header->size);
        dawn_free(ptr);
    }
    return new_ptr;
}

/*
 * String Functions
 */
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

int dawn_strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (s1 == NULL || s2 == NULL) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *dawn_strcpy(char *dest, const char *src) {
    if (dest == NULL || src == NULL) return dest;
    size_t i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

char *dawn_strncpy(char *dest, const char *src, size_t n) {
    if (dest == NULL || src == NULL) return dest;
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    while (i < n) {
        dest[i] = '\0';
        i++;
    }
    return dest;
}

char *dawn_strcat(char *dest, const char *src) {
    if (dest == NULL || src == NULL) return dest;
    size_t dlen = dawn_strlen(dest);
    size_t i = 0;
    while (src[i] != '\0') {
        dest[dlen + i] = src[i];
        i++;
    }
    dest[dlen + i] = '\0';
    return dest;
}

char *dawn_strchr(const char *s, int c) {
    if (s == NULL) return NULL;
    while (*s != '\0') {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
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

int dawn_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

int dawn_atoi(const char *str) {
    if (str == NULL) return 0;
    int res = 0;
    int sign = 1;
    while (*str == ' ' || *str == '\t' || *str == '\n') str++;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

/*
 * Console I/O & Formatted Output
 */
void dawn_putchar(char c) {
    dawn_write(DAWN_STDOUT_FILENO, &c, 1);
}

char dawn_getchar(void) {
    char c = 0;
    while (dawn_read(DAWN_STDIN_FILENO, &c, 1) <= 0) {
        dawn_yield();
    }
    return c;
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
    dawn_putchar('\n');
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

int dawn_snprintf(char *buf, size_t max_len, const char *fmt, ...) {
    if (buf == NULL || max_len == 0 || fmt == NULL) return 0;
    va_list args;
    va_start(args, fmt);
    size_t out_idx = 0;

    for (size_t i = 0; fmt[i] != '\0' && out_idx + 1 < max_len; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            if (fmt[i] == 's') {
                const char *s = va_arg(args, const char *);
                if (s == NULL) s = "(null)";
                while (*s != '\0' && out_idx + 1 < max_len) {
                    buf[out_idx++] = *s++;
                }
            } else if (fmt[i] == 'd') {
                int d = va_arg(args, int);
                char num_buf[16];
                int n_idx = 0;
                if (d < 0) {
                    if (out_idx + 1 < max_len) buf[out_idx++] = '-';
                    d = -d;
                }
                if (d == 0) {
                    if (out_idx + 1 < max_len) buf[out_idx++] = '0';
                } else {
                    while (d > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (d % 10));
                        d /= 10;
                    }
                    while (n_idx > 0 && out_idx + 1 < max_len) {
                        buf[out_idx++] = num_buf[--n_idx];
                    }
                }
            } else if (fmt[i] == 'u') {
                unsigned int u = va_arg(args, unsigned int);
                char num_buf[16];
                int n_idx = 0;
                if (u == 0) {
                    if (out_idx + 1 < max_len) buf[out_idx++] = '0';
                } else {
                    while (u > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (u % 10));
                        u /= 10;
                    }
                    while (n_idx > 0 && out_idx + 1 < max_len) {
                        buf[out_idx++] = num_buf[--n_idx];
                    }
                }
            } else if (fmt[i] == 'x' || fmt[i] == 'p') {
                uint64_t x = va_arg(args, uint64_t);
                static const char hex_chars[] = "0123456789abcdef";
                char hex_buf[16];
                int h_idx = 0;
                if (fmt[i] == 'p') {
                    if (out_idx + 2 < max_len) {
                        buf[out_idx++] = '0';
                        buf[out_idx++] = 'x';
                    }
                }
                if (x == 0) {
                    if (out_idx + 1 < max_len) buf[out_idx++] = '0';
                } else {
                    while (x > 0 && h_idx < 16) {
                        hex_buf[h_idx++] = hex_chars[x & 0x0f];
                        x >>= 4;
                    }
                    while (h_idx > 0 && out_idx + 1 < max_len) {
                        buf[out_idx++] = hex_buf[--h_idx];
                    }
                }
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(args, int);
                buf[out_idx++] = c;
            } else if (fmt[i] == '%') {
                buf[out_idx++] = '%';
            }
        } else {
            buf[out_idx++] = fmt[i];
        }
    }
    buf[out_idx] = '\0';
    va_end(args);
    return (int)out_idx;
}

int dawn_printf(const char *fmt, ...) {
    char pbuf[512];
    va_list args;
    va_start(args, fmt);
    size_t out_idx = 0;

    for (size_t i = 0; fmt[i] != '\0' && out_idx + 1 < sizeof(pbuf); i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            if (fmt[i] == 's') {
                const char *s = va_arg(args, const char *);
                if (s == NULL) s = "(null)";
                while (*s != '\0' && out_idx + 1 < sizeof(pbuf)) {
                    pbuf[out_idx++] = *s++;
                }
            } else if (fmt[i] == 'd') {
                int d = va_arg(args, int);
                char num_buf[16];
                int n_idx = 0;
                if (d < 0) {
                    if (out_idx + 1 < sizeof(pbuf)) pbuf[out_idx++] = '-';
                    d = -d;
                }
                if (d == 0) {
                    if (out_idx + 1 < sizeof(pbuf)) pbuf[out_idx++] = '0';
                } else {
                    while (d > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (d % 10));
                        d /= 10;
                    }
                    while (n_idx > 0 && out_idx + 1 < sizeof(pbuf)) {
                        pbuf[out_idx++] = num_buf[--n_idx];
                    }
                }
            } else if (fmt[i] == 'u') {
                unsigned int u = va_arg(args, unsigned int);
                char num_buf[16];
                int n_idx = 0;
                if (u == 0) {
                    if (out_idx + 1 < sizeof(pbuf)) pbuf[out_idx++] = '0';
                } else {
                    while (u > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (u % 10));
                        u /= 10;
                    }
                    while (n_idx > 0 && out_idx + 1 < sizeof(pbuf)) {
                        pbuf[out_idx++] = num_buf[--n_idx];
                    }
                }
            } else if (fmt[i] == 'x' || fmt[i] == 'p') {
                uint64_t x = va_arg(args, uint64_t);
                static const char hex_chars[] = "0123456789abcdef";
                char hex_buf[16];
                int h_idx = 0;
                if (fmt[i] == 'p') {
                    if (out_idx + 2 < sizeof(pbuf)) {
                        pbuf[out_idx++] = '0';
                        pbuf[out_idx++] = 'x';
                    }
                }
                if (x == 0) {
                    if (out_idx + 1 < sizeof(pbuf)) pbuf[out_idx++] = '0';
                } else {
                    while (x > 0 && h_idx < 16) {
                        hex_buf[h_idx++] = hex_chars[x & 0x0f];
                        x >>= 4;
                    }
                    while (h_idx > 0 && out_idx + 1 < sizeof(pbuf)) {
                        pbuf[out_idx++] = hex_buf[--h_idx];
                    }
                }
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(args, int);
                pbuf[out_idx++] = c;
            } else if (fmt[i] == '%') {
                pbuf[out_idx++] = '%';
            }
        } else {
            pbuf[out_idx++] = fmt[i];
        }
    }
    pbuf[out_idx] = '\0';
    va_end(args);
    dawn_print(pbuf);
    return (int)out_idx;
}

size_t dawn_readline(char *buf, size_t max_len) {
    if (buf == NULL || max_len == 0) return 0;
    size_t idx = 0;
    while (idx + 1 < max_len) {
        char c = dawn_getchar();
        if (c == '\r' || c == '\n') {
            dawn_putchar('\n');
            break;
        } else if (c == '\b' || c == 0x7f) {
            if (idx > 0) {
                idx--;
                dawn_print("\b \b");
            }
        } else if (c >= ' ' && c <= '~') {
            buf[idx++] = c;
            dawn_putchar(c);
        }
    }
    buf[idx] = '\0';
    return idx;
}

_Noreturn void dawn_exit(int64_t status) {
    dawn_syscall1(DAWN_SYS_EXIT, (uint64_t)status);
    for (;;) {
        __asm__ volatile ("pause");
    }
}
