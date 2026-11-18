/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_APP_RUNTIME_H
#define DAWN_APP_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_INSTALLED_PACKAGES 16U
#define MAX_RUNNING_APPS       16U
#define DLIBC_HEAP_SIZE        (64U * 1024U) /* 64 KiB runtime user heap pool */

/* Application Manifest and Package Format */
struct app_manifest {
    char     package_id[32];   /* e.g. "org.dawn.calc" */
    char     name[32];         /* e.g. "Calculator" */
    char     version[16];      /* e.g. "1.0.0" */
    char     description[64];  /* e.g. "Native arithmetic calculator" */
    char     binary_path[64];  /* e.g. "/bin/calc.elf" */
    uint32_t icon_id;
    uint32_t required_permissions;
    bool     installed;
};

/* Application Runtime Instance */
enum app_state {
    APP_STATE_IDLE = 0,
    APP_STATE_STARTING = 1,
    APP_STATE_RUNNING = 2,
    APP_STATE_TERMINATED = 3
};

struct app_instance {
    uint32_t       app_id;
    uint64_t       pid;
    char           package_id[32];
    enum app_state state;
    int            window_id;
    bool           active;
};

/* Standalone C Library Subset (dlibc) */
size_t dlibc_strlen(const char *s);
int    dlibc_strcmp(const char *s1, const char *s2);
int    dlibc_strncmp(const char *s1, const char *s2, size_t n);
char  *dlibc_strcpy(char *dest, const char *src);
char  *dlibc_strncpy(char *dest, const char *src, size_t n);
void  *dlibc_memset(void *s, int c, size_t n);
void  *dlibc_memcpy(void *dest, const void *src, size_t n);
int    dlibc_memcmp(const void *s1, const void *s2, size_t n);

/* User Heap Allocator */
void  *dlibc_malloc(size_t size);
void   dlibc_free(void *ptr);

/* Package Manager API */
bool pkg_install(const struct app_manifest *manifest);
bool pkg_remove(const char *package_id);
const struct app_manifest *pkg_find(const char *package_id);
size_t pkg_get_installed(struct app_manifest *out_array, size_t max_count);

/* Application Lifecycle API */
int  app_launch(const char *package_id, uint64_t *out_pid);
bool app_terminate(int app_id);
struct app_instance *app_get_instance(int app_id);

/* Subsystem Init & Self-test */
bool app_runtime_initialize(void);
bool app_runtime_self_test(void);

#endif /* DAWN_APP_RUNTIME_H */
