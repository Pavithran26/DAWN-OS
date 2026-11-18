/* SPDX-License-Identifier: Unlicense */
#include "app_runtime.h"
#include "process.h"
#include "vfs.h"

static struct app_manifest package_table[MAX_INSTALLED_PACKAGES];
static struct app_instance app_instances[MAX_RUNNING_APPS];
static uint32_t next_app_id = 1;
static bool runtime_initialized = false;

/* Heap Memory Pool */
static uint8_t heap_storage[DLIBC_HEAP_SIZE] __attribute__((aligned(8)));

struct heap_chunk_header {
    size_t size;
    bool is_free;
    struct heap_chunk_header *next;
};

static struct heap_chunk_header *heap_head = NULL;

/* dlibc implementation */
size_t dlibc_strlen(const char *s) {
    if (s == NULL) return 0;
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

int dlibc_strcmp(const char *s1, const char *s2) {
    if (s1 == NULL || s2 == NULL) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)((uint8_t)*s1 - (uint8_t)*s2);
}

int dlibc_strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0 || s1 == NULL || s2 == NULL) return 0;
    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return (int)((uint8_t)*s1 - (uint8_t)*s2);
}

char *dlibc_strcpy(char *dest, const char *src) {
    if (dest == NULL || src == NULL) return dest;
    char *ret = dest;
    while ((*dest++ = *src++) != '\0') {}
    return ret;
}

char *dlibc_strncpy(char *dest, const char *src, size_t n) {
    if (dest == NULL || src == NULL || n == 0) return dest;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    void *ret = s;
    __asm__ volatile (
        "rep stosb"
        : "+D"(s), "+c"(n)
        : "a"((uint8_t)c)
        : "memory"
    );
    return ret;
}

void *memcpy(void *dest, const void *src, size_t n) {
    void *ret = dest;
    __asm__ volatile (
        "rep movsb"
        : "+D"(dest), "+S"(src), "+c"(n)
        :
        : "memory"
    );
    return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
    void *ret = dest;
    if (dest < src) {
        __asm__ volatile (
            "rep movsb"
            : "+D"(dest), "+S"(src), "+c"(n)
            :
            : "memory"
        );
    } else if (dest > src) {
        uint8_t *d = (uint8_t *)dest + n - 1;
        const uint8_t *s = (const uint8_t *)src + n - 1;
        __asm__ volatile (
            "std\n\t"
            "rep movsb\n\t"
            "cld"
            : "+D"(d), "+S"(s), "+c"(n)
            :
            : "memory"
        );
    }
    return ret;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) return (int)(*p1 - *p2);
        p1++;
        p2++;
    }
    return 0;
}

void *dlibc_memset(void *s, int c, size_t n) {
    return memset(s, c, n);
}

void *dlibc_memcpy(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

int dlibc_memcmp(const void *s1, const void *s2, size_t n) {
    return memcmp(s1, s2, n);
}

/* Heap Allocator */
static void heap_init(void) {
    heap_head = (struct heap_chunk_header *)((void *)heap_storage);
    heap_head->size = DLIBC_HEAP_SIZE - sizeof(struct heap_chunk_header);
    heap_head->is_free = true;
    heap_head->next = NULL;
}

void *dlibc_malloc(size_t size) {
    if (size == 0) return NULL;
    if (heap_head == NULL) heap_init();

    /* 8-byte alignment */
    size = (size + 7U) & ~7U;

    struct heap_chunk_header *curr = heap_head;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            /* Check if we can split block */
            if (curr->size >= size + sizeof(struct heap_chunk_header) + 16U) {
                struct heap_chunk_header *new_block = (struct heap_chunk_header *)((uint8_t *)curr + sizeof(struct heap_chunk_header) + size);
                new_block->size = curr->size - size - sizeof(struct heap_chunk_header);
                new_block->is_free = true;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = false;
            return (void *)((uint8_t *)curr + sizeof(struct heap_chunk_header));
        }
        curr = curr->next;
    }

    return NULL;
}

void dlibc_free(void *ptr) {
    if (ptr == NULL || heap_head == NULL) return;

    struct heap_chunk_header *chunk = (struct heap_chunk_header *)((uint8_t *)ptr - sizeof(struct heap_chunk_header));
    chunk->is_free = true;

    /* Coalesce consecutive free chunks */
    struct heap_chunk_header *curr = heap_head;
    while (curr != NULL && curr->next != NULL) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(struct heap_chunk_header) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

/* Package Manager */
bool pkg_install(const struct app_manifest *manifest) {
    if (manifest == NULL) return false;

    /* Check if already installed */
    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES; i++) {
        if (package_table[i].installed && dlibc_strcmp(package_table[i].package_id, manifest->package_id) == 0) {
            package_table[i] = *manifest;
            package_table[i].installed = true;
            return true;
        }
    }

    /* Find empty slot */
    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES; i++) {
        if (!package_table[i].installed) {
            package_table[i] = *manifest;
            package_table[i].installed = true;
            return true;
        }
    }

    return false;
}

bool pkg_remove(const char *package_id) {
    if (package_id == NULL) return false;

    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES; i++) {
        if (package_table[i].installed && dlibc_strcmp(package_table[i].package_id, package_id) == 0) {
            package_table[i].installed = false;
            return true;
        }
    }

    return false;
}

const struct app_manifest *pkg_find(const char *package_id) {
    if (package_id == NULL) return NULL;

    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES; i++) {
        if (package_table[i].installed && dlibc_strcmp(package_table[i].package_id, package_id) == 0) {
            return &package_table[i];
        }
    }

    return NULL;
}

size_t pkg_get_installed(struct app_manifest *out_array, size_t max_count) {
    if (out_array == NULL || max_count == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES && count < max_count; i++) {
        if (package_table[i].installed) {
            out_array[count++] = package_table[i];
        }
    }

    return count;
}

/* Application Lifecycle */
int app_launch(const char *package_id, uint64_t *out_pid) {
    if (package_id == NULL) return -1;

    const struct app_manifest *pkg = pkg_find(package_id);
    if (pkg == NULL) return -1;

    for (size_t i = 0; i < MAX_RUNNING_APPS; i++) {
        if (!app_instances[i].active) {
            struct app_instance *inst = &app_instances[i];
            inst->app_id = next_app_id++;
            inst->pid = 100ULL + inst->app_id;
            dlibc_strncpy(inst->package_id, package_id, sizeof(inst->package_id));
            inst->state = APP_STATE_RUNNING;
            inst->window_id = -1;
            inst->active = true;

            if (out_pid != NULL) *out_pid = inst->pid;
            return (int)inst->app_id;
        }
    }

    return -1;
}

bool app_terminate(int app_id) {
    for (size_t i = 0; i < MAX_RUNNING_APPS; i++) {
        if (app_instances[i].active && app_instances[i].app_id == (uint32_t)app_id) {
            app_instances[i].state = APP_STATE_TERMINATED;
            app_instances[i].active = false;
            return true;
        }
    }
    return false;
}

struct app_instance *app_get_instance(int app_id) {
    for (size_t i = 0; i < MAX_RUNNING_APPS; i++) {
        if (app_instances[i].active && app_instances[i].app_id == (uint32_t)app_id) {
            return &app_instances[i];
        }
    }
    return NULL;
}

bool app_runtime_initialize(void) {
    if (runtime_initialized) return true;

    heap_init();

    for (size_t i = 0; i < MAX_INSTALLED_PACKAGES; i++) package_table[i] = (struct app_manifest){0};
    for (size_t i = 0; i < MAX_RUNNING_APPS; i++) app_instances[i] = (struct app_instance){0};
    next_app_id = 1;

    /* Install native default system applications */
    struct app_manifest terminal_pkg = {
        .package_id = "org.dawn.terminal",
        .name = "DAWN Terminal",
        .version = "1.0.0",
        .description = "Native ANSI terminal emulator",
        .binary_path = "/bin/terminal",
        .icon_id = 0,
        .required_permissions = 0,
        .installed = true
    };
    pkg_install(&terminal_pkg);

    struct app_manifest filemgr_pkg = {
        .package_id = "org.dawn.filemgr",
        .name = "File Explorer",
        .version = "1.0.0",
        .description = "Native VFS file manager",
        .binary_path = "/bin/filemgr",
        .icon_id = 1,
        .required_permissions = 0,
        .installed = true
    };
    pkg_install(&filemgr_pkg);

    struct app_manifest taskmgr_pkg = {
        .package_id = "org.dawn.taskmgr",
        .name = "Task Manager",
        .version = "1.0.0",
        .description = "Process and CPU/RAM monitor",
        .binary_path = "/bin/taskmgr",
        .icon_id = 2,
        .required_permissions = 0,
        .installed = true
    };
    pkg_install(&taskmgr_pkg);

    struct app_manifest settings_pkg = {
        .package_id = "org.dawn.settings",
        .name = "System Settings",
        .version = "1.0.0",
        .description = "System configuration manager",
        .binary_path = "/bin/settings",
        .icon_id = 3,
        .required_permissions = 0,
        .installed = true
    };
    pkg_install(&settings_pkg);

    runtime_initialized = true;
    return true;
}

/* Self-test */
bool app_runtime_self_test(void) {
    if (!app_runtime_initialize()) return false;

    /* dlibc string tests */
    if (dlibc_strlen("DAWN-OS") != 7) return false;
    if (dlibc_strcmp("abc", "abc") != 0) return false;
    if (dlibc_strcmp("abc", "abd") >= 0) return false;
    if (dlibc_strncmp("abcdef", "abcxyz", 3) != 0) return false;

    char test_str[32] = {0};
    dlibc_strcpy(test_str, "hello");
    if (dlibc_strcmp(test_str, "hello") != 0) return false;

    /* dlibc memory tests */
    uint8_t mem_buf[16];
    dlibc_memset(mem_buf, 0xAA, sizeof(mem_buf));
    for (size_t i = 0; i < sizeof(mem_buf); i++) {
        if (mem_buf[i] != 0xAA) return false;
    }

    uint8_t mem_copy[16];
    dlibc_memcpy(mem_copy, mem_buf, sizeof(mem_copy));
    if (dlibc_memcmp(mem_copy, mem_buf, sizeof(mem_copy)) != 0) return false;

    /* Heap allocator tests */
    void *ptr1 = dlibc_malloc(128);
    if (ptr1 == NULL) return false;
    dlibc_memset(ptr1, 0x55, 128);

    void *ptr2 = dlibc_malloc(256);
    if (ptr2 == NULL) return false;
    dlibc_memset(ptr2, 0xAA, 256);

    dlibc_free(ptr1);
    dlibc_free(ptr2);

    /* Package manager query */
    const struct app_manifest *m = pkg_find("org.dawn.terminal");
    if (m == NULL || dlibc_strcmp(m->name, "DAWN Terminal") != 0) return false;

    /* Application launch and termination */
    uint64_t app_pid = 0;
    int app_id = app_launch("org.dawn.terminal", &app_pid);
    if (app_id < 0 || app_pid == 0) return false;

    struct app_instance *inst = app_get_instance(app_id);
    if (inst == NULL || inst->state != APP_STATE_RUNNING) return false;

    if (!app_terminate(app_id)) return false;
    if (app_get_instance(app_id) != NULL) return false;

    return true;
}
