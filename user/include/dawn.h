/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USER_H
#define DAWN_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_SYSCALL_VECTOR 0x80U

#define DAWN_SYS_ABI_VERSION 0ULL
#define DAWN_SYS_WRITE       1ULL
#define DAWN_SYS_EXIT        2ULL
#define DAWN_SYS_GETPID      3ULL
#define DAWN_SYS_YIELD       4ULL
#define DAWN_SYS_WAIT        5ULL
#define DAWN_SYS_READ        6ULL
#define DAWN_SYS_CLOSE       7ULL
#define DAWN_SYS_DUP         8ULL
#define DAWN_SYS_SPAWN       9ULL
#define DAWN_SYS_EXEC        10ULL

#define DAWN_STDIN_FILENO  0ULL
#define DAWN_STDOUT_FILENO 1ULL
#define DAWN_STDERR_FILENO 2ULL

static inline int64_t dawn_syscall0(uint64_t num) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t dawn_syscall1(uint64_t num, uint64_t a0) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(a0) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t dawn_syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(a0), "S"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t dawn_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t dawn_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

/* System call wrappers */
static inline int64_t dawn_abi_version(void) {
    return dawn_syscall0(DAWN_SYS_ABI_VERSION);
}

static inline int64_t dawn_write(uint64_t fd, const void *buf, uint64_t count) {
    return dawn_syscall3(DAWN_SYS_WRITE, fd, (uint64_t)buf, count);
}

static inline int64_t dawn_read(uint64_t fd, void *buf, uint64_t count) {
    return dawn_syscall3(DAWN_SYS_READ, fd, (uint64_t)buf, count);
}

static inline _Noreturn void dawn_exit(int64_t status) {
    dawn_syscall1(DAWN_SYS_EXIT, (uint64_t)status);
    for (;;) __asm__ volatile ("pause");
}

static inline uint64_t dawn_getpid(void) {
    return (uint64_t)dawn_syscall0(DAWN_SYS_GETPID);
}

static inline void dawn_yield(void) {
    dawn_syscall0(DAWN_SYS_YIELD);
}

static inline int64_t dawn_wait(int64_t pid, int64_t *status) {
    return dawn_syscall2(DAWN_SYS_WAIT, (uint64_t)pid, (uint64_t)status);
}

static inline int64_t dawn_close(uint64_t fd) {
    return dawn_syscall1(DAWN_SYS_CLOSE, fd);
}

static inline int64_t dawn_dup(uint64_t oldfd) {
    return dawn_syscall1(DAWN_SYS_DUP, oldfd);
}

static inline int64_t dawn_spawn(const void *image, uint64_t image_size, const char *const argv[], const char *const envp[]) {
    return dawn_syscall4(DAWN_SYS_SPAWN, (uint64_t)image, image_size, (uint64_t)argv, (uint64_t)envp);
}

static inline int64_t dawn_exec(const void *image, uint64_t image_size, const char *const argv[], const char *const envp[]) {
    return dawn_syscall4(DAWN_SYS_EXEC, (uint64_t)image, image_size, (uint64_t)argv, (uint64_t)envp);
}

/* Userspace runtime helper utilities */
size_t dawn_strlen(const char *str);
int dawn_strcmp(const char *s1, const char *s2);
void *dawn_memcpy(void *dest, const void *src, size_t n);
void *dawn_memset(void *s, int c, size_t n);
void dawn_print(const char *str);
void dawn_println(const char *str);
void dawn_print_hex(uint64_t val);
void dawn_print_dec(uint64_t val);

#endif
