/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USER_H
#define DAWN_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

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
#define DAWN_SYS_OPEN        11ULL
#define DAWN_SYS_BRK         12ULL
#define DAWN_SYS_TIME        13ULL
#define DAWN_SYS_GETCWD      14ULL
#define DAWN_SYS_CHDIR       15ULL
#define DAWN_SYS_UNLINK      16ULL
#define DAWN_SYS_DUP2        17ULL
#define DAWN_SYS_MKDIR       18ULL
#define DAWN_SYS_STAT        19ULL
#define DAWN_SYS_LSEEK       20ULL
#define DAWN_SYS_GETDENTS    21ULL
#define DAWN_SYS_KILL        22ULL

#define DAWN_O_RDONLY 0x0001U
#define DAWN_O_WRONLY 0x0002U
#define DAWN_O_RDWR   0x0003U

#define DAWN_SEEK_SET 0
#define DAWN_SEEK_CUR 1
#define DAWN_SEEK_END 2

struct dawn_posix_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

struct dawn_dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[128];
    uint64_t d_size;
};

#define DAWN_STDIN_FILENO  0ULL
#define DAWN_STDOUT_FILENO 1ULL
#define DAWN_STDERR_FILENO 2ULL

/*
 * x86-64 Fast Syscall Invocations
 */
static inline int64_t dawn_syscall0(uint64_t num) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t dawn_syscall1(uint64_t num, uint64_t a0) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a0)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t dawn_syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a0), "S"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t dawn_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a0), "S"(a1), "d"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t dawn_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* System call wrappers */
static inline int64_t dawn_abi_version(void) {
    return dawn_syscall0(DAWN_SYS_ABI_VERSION);
}

static inline int64_t dawn_write(uint64_t fd, const void *buf, uint64_t count) {
    return dawn_syscall3(DAWN_SYS_WRITE, fd, (uint64_t)(uintptr_t)buf, count);
}

static inline int64_t dawn_read(uint64_t fd, void *buf, uint64_t count) {
    return dawn_syscall3(DAWN_SYS_READ, fd, (uint64_t)(uintptr_t)buf, count);
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
    return dawn_syscall2(DAWN_SYS_WAIT, (uint64_t)pid, (uint64_t)(uintptr_t)status);
}

static inline int64_t dawn_close(uint64_t fd) {
    return dawn_syscall1(DAWN_SYS_CLOSE, fd);
}

static inline int64_t dawn_dup(uint64_t oldfd) {
    return dawn_syscall1(DAWN_SYS_DUP, oldfd);
}

static inline int64_t dawn_dup2(uint64_t oldfd, uint64_t newfd) {
    return dawn_syscall2(DAWN_SYS_DUP2, oldfd, newfd);
}

static inline int64_t dawn_open(const char *path, uint64_t flags, uint64_t mode) {
    return dawn_syscall3(DAWN_SYS_OPEN, (uint64_t)(uintptr_t)path, flags, mode);
}

static inline void *dawn_brk(void *addr) {
    int64_t ret = dawn_syscall1(DAWN_SYS_BRK, (uint64_t)(uintptr_t)addr);
    return (void *)(uintptr_t)ret;
}

static inline void *dawn_sbrk(intptr_t increment) {
    void *current_brk = dawn_brk(NULL);
    if (increment == 0) return current_brk;
    void *new_brk = (void *)((uintptr_t)current_brk + (uintptr_t)increment);
    void *res = dawn_brk(new_brk);
    if (res == current_brk && increment > 0) return (void *)-1;
    return current_brk;
}

static inline int64_t dawn_time(uint64_t *tloc) {
    return dawn_syscall1(DAWN_SYS_TIME, (uint64_t)(uintptr_t)tloc);
}

static inline int64_t dawn_getcwd(char *buf, size_t size) {
    return dawn_syscall2(DAWN_SYS_GETCWD, (uint64_t)(uintptr_t)buf, size);
}

static inline int64_t dawn_chdir(const char *path) {
    return dawn_syscall1(DAWN_SYS_CHDIR, (uint64_t)(uintptr_t)path);
}

static inline int64_t dawn_unlink(const char *path) {
    return dawn_syscall1(DAWN_SYS_UNLINK, (uint64_t)(uintptr_t)path);
}

static inline int64_t dawn_mkdir(const char *path, uint64_t mode) {
    return dawn_syscall2(DAWN_SYS_MKDIR, (uint64_t)(uintptr_t)path, mode);
}

static inline int64_t dawn_stat(const char *path, struct dawn_posix_stat *buf) {
    return dawn_syscall2(DAWN_SYS_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)buf);
}

static inline int64_t dawn_lseek(uint64_t fd, int64_t offset, int whence) {
    return dawn_syscall3(DAWN_SYS_LSEEK, fd, (uint64_t)offset, (uint64_t)whence);
}

static inline int64_t dawn_getdents(uint64_t fd, struct dawn_dirent *dirp, size_t count) {
    return dawn_syscall3(DAWN_SYS_GETDENTS, fd, (uint64_t)(uintptr_t)dirp, count);
}

static inline int64_t dawn_kill(uint64_t pid, int sig) {
    return dawn_syscall2(DAWN_SYS_KILL, pid, (uint64_t)sig);
}

static inline int64_t dawn_spawn(const void *image, uint64_t image_size, const char *const argv[], const char *const envp[]) {
    return dawn_syscall4(DAWN_SYS_SPAWN, (uint64_t)(uintptr_t)image, image_size, (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp);
}

static inline int64_t dawn_exec(const void *image, uint64_t image_size, const char *const argv[], const char *const envp[]) {
    return dawn_syscall4(DAWN_SYS_EXEC, (uint64_t)(uintptr_t)image, image_size, (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp);
}

/*
 * C Standard Library functions & Memory Allocator
 */
void *dawn_malloc(size_t size);
void dawn_free(void *ptr);
void *dawn_calloc(size_t nmemb, size_t size);
void *dawn_realloc(void *ptr, size_t size);

/* String & I/O helpers */
size_t dawn_strlen(const char *str);
int dawn_strcmp(const char *s1, const char *s2);
int dawn_strncmp(const char *s1, const char *s2, size_t n);
char *dawn_strcpy(char *dest, const char *src);
char *dawn_strncpy(char *dest, const char *src, size_t n);
char *dawn_strcat(char *dest, const char *src);
char *dawn_strchr(const char *s, int c);
void *dawn_memcpy(void *dest, const void *src, size_t n);
void *dawn_memset(void *s, int c, size_t n);
int dawn_memcmp(const void *s1, const void *s2, size_t n);
int dawn_atoi(const char *str);

/* Printing & Console I/O */
void dawn_putchar(char c);
char dawn_getchar(void);
void dawn_print(const char *str);
void dawn_println(const char *str);
void dawn_print_hex(uint64_t val);
void dawn_print_dec(uint64_t val);
int dawn_printf(const char *fmt, ...);
int dawn_snprintf(char *buf, size_t max_len, const char *fmt, ...);
size_t dawn_readline(char *buf, size_t max_len);

#endif
