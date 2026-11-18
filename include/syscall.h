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
 * SYS_EXIT (status in RDI), SYS_YIELD, and SYS_WAIT (target_pid in RDI,
 * status_address in RSI) are handled by the CPL3 scheduler path; the
 * kernel-callable dispatcher returns ENOSYS for them because it has no user
 * thread to terminate, yield, or block. SYS_GETPID returns the active process ID.
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
    DAWN_SYS_WAIT = 5
};

enum dawn_syscall_errno {
    DAWN_ERRNO_EPERM = 1,
    DAWN_ERRNO_ESRCH = 3,
    DAWN_ERRNO_EIO = 5,
    DAWN_ERRNO_EBADF = 9,
    DAWN_ERRNO_ECHILD = 10,
    DAWN_ERRNO_EAGAIN = 11,
    DAWN_ERRNO_ENOMEM = 12,
    DAWN_ERRNO_EFAULT = 14,
    DAWN_ERRNO_EINVAL = 22,
    DAWN_ERRNO_ENOSYS = 38
};

#define DAWN_STDOUT_FD 1ULL
#define DAWN_STDERR_FD 2ULL

int64_t syscall_dispatch(uint64_t number,
                         uint64_t argument0, uint64_t argument1,
                         uint64_t argument2, uint64_t argument3,
                         uint64_t argument4, uint64_t argument5);
#ifdef DAWN_KERNEL_TEST
bool syscall_self_test(void);
#endif
#endif
