/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SYSCALL_H
#define DAWN_SYSCALL_H
#include <stdbool.h>
#include <stdint.h>

/*
 * DAWN userspace syscall ABI v1 (x86-64):
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = arguments 0..5
 *   RAX = non-negative result or negative DAWN_ERRNO_* value
 *
 * Supported entry paths:
 *   1. Hardware fast syscall: 'syscall' instruction (MSR STAR/LSTAR/FMASK)
 *   2. Software interrupt: 'int $0x80' instruction (IDT DPL3 gate)
 */
#define DAWN_SYSCALL_ABI_VERSION 1ULL
#define DAWN_SYSCALL_VECTOR 0x80U
#define DAWN_SYSCALL_MAX_WRITE 4096ULL

enum dawn_syscall_number {
    DAWN_SYS_ABI_VERSION = 0,
    DAWN_SYS_WRITE = 1,
    DAWN_SYS_EXIT = 2,
    DAWN_SYS_GETPID = 3,
    DAWN_SYS_YIELD = 4,
    DAWN_SYS_WAIT = 5,
    DAWN_SYS_READ = 6,
    DAWN_SYS_CLOSE = 7,
    DAWN_SYS_DUP = 8,
    DAWN_SYS_SPAWN = 9,
    DAWN_SYS_EXEC = 10,
    /* Core POSIX additions */
    DAWN_SYS_OPEN = 11,
    DAWN_SYS_BRK = 12,
    DAWN_SYS_TIME = 13,
    DAWN_SYS_GETCWD = 14,
    DAWN_SYS_CHDIR = 15,
    DAWN_SYS_UNLINK = 16,
    DAWN_SYS_DUP2 = 17,
    DAWN_SYS_MKDIR = 18,
    DAWN_SYS_STAT = 19,
    DAWN_SYS_LSEEK = 20,
    DAWN_SYS_GETDENTS = 21,
    DAWN_SYS_KILL = 22,
    DAWN_SYS_SYSINFO = 23
};

struct dawn_sysinfo_user {
    uint32_t cpu_count;
    uint32_t online_cpu_count;
    uint64_t total_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t uptime_seconds;
    char cpu_model[64];
    char boot_mode[16];
};

struct dawn_dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[128];
    uint64_t d_size;
};

enum dawn_syscall_errno {
    DAWN_ERRNO_EPERM = 1,
    DAWN_ERRNO_ENOENT = 2,
    DAWN_ERRNO_ESRCH = 3,
    DAWN_ERRNO_EIO = 5,
    DAWN_ERRNO_EBADF = 9,
    DAWN_ERRNO_ECHILD = 10,
    DAWN_ERRNO_EAGAIN = 11,
    DAWN_ERRNO_ENOMEM = 12,
    DAWN_ERRNO_EFAULT = 14,
    DAWN_ERRNO_EINVAL = 22,
    DAWN_ERRNO_EMFILE = 24,
    DAWN_ERRNO_ENAMETOOLONG = 36,
    DAWN_ERRNO_ENOSYS = 38
};

#define DAWN_STDOUT_FD 1ULL
#define DAWN_STDERR_FD 2ULL

bool syscall_init_hardware(void);

int64_t syscall_dispatch(uint64_t number,
                         uint64_t argument0, uint64_t argument1,
                         uint64_t argument2, uint64_t argument3,
                         uint64_t argument4, uint64_t argument5);
#ifdef DAWN_KERNEL_TEST
bool syscall_self_test(void);
#endif
#endif
