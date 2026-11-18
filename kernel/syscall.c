/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syscall.h"
#include "page_table.h"
#include "process.h"

#define SERIAL_COM1 0x3f8U
#define SERIAL_POLL_LIMIT 100000U
#define SYSCALL_WRITE_CHUNK 128U

static inline void outb(uint16_t port,uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value=0U;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static bool serial_write_byte(uint8_t value) {
    for (uint32_t attempt=0U;attempt<SERIAL_POLL_LIMIT;attempt++) {
        if ((inb((uint16_t)(SERIAL_COM1+5U))&0x20U)!=0U) {
            outb((uint16_t)SERIAL_COM1,value);
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static int64_t syscall_write(uint64_t descriptor,uint64_t user_buffer,
                             uint64_t count) {
    uint8_t temporary[SYSCALL_WRITE_CHUNK];
    uint64_t written=0ULL;
    if (descriptor!=DAWN_STDOUT_FD && descriptor!=DAWN_STDERR_FD)
        return -(int64_t)DAWN_ERRNO_EBADF;
    if (count>DAWN_SYSCALL_MAX_WRITE)
        return -(int64_t)DAWN_ERRNO_EINVAL;
    if (count==0ULL) return 0;
    /* Validate the entire range before producing any externally visible output. */
    if (!vm_user_range_valid(user_buffer,count,false))
        return -(int64_t)DAWN_ERRNO_EFAULT;
    while (written<count) {
        const uint64_t remaining=count-written;
        const size_t chunk=(remaining>(uint64_t)sizeof(temporary))
                               ? sizeof(temporary) : (size_t)remaining;
        if (!vm_copy_from_user(temporary,user_buffer+written,chunk))
            return written!=0ULL ? (int64_t)written
                                 : -(int64_t)DAWN_ERRNO_EFAULT;
        for (size_t index=0U;index<chunk;index++) {
            if (!serial_write_byte(temporary[index]))
                return written!=0ULL ? (int64_t)written
                                     : -(int64_t)DAWN_ERRNO_EIO;
            ++written;
        }
    }
    return (int64_t)written;
}

int64_t syscall_dispatch(uint64_t number,
                         uint64_t argument0,uint64_t argument1,
                         uint64_t argument2,uint64_t argument3,
                         uint64_t argument4,uint64_t argument5) {
    (void)argument3;
    (void)argument4;
    (void)argument5;
    switch (number) {
        case DAWN_SYS_ABI_VERSION:
            return (int64_t)DAWN_SYSCALL_ABI_VERSION;
        case DAWN_SYS_WRITE:
            return syscall_write(argument0,argument1,argument2);
        case DAWN_SYS_GETPID:
            return (int64_t)process_active_id();
        case DAWN_SYS_EXIT:
        case DAWN_SYS_YIELD:
        case DAWN_SYS_WAIT:
        default:
            return -(int64_t)DAWN_ERRNO_ENOSYS;
    }
}

#ifdef DAWN_KERNEL_TEST
bool syscall_self_test(void) {
    if (syscall_dispatch(DAWN_SYS_ABI_VERSION,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=(int64_t)DAWN_SYSCALL_ABI_VERSION)
        return false;
    if (syscall_dispatch(DAWN_SYS_GETPID,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=0)
        return false;
    if (syscall_dispatch(DAWN_SYS_YIELD,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    if (syscall_dispatch(DAWN_SYS_WAIT,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    if (syscall_dispatch(DAWN_SYS_WRITE,DAWN_STDOUT_FD,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=0) return false;
    if (syscall_dispatch(DAWN_SYS_WRITE,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    if (syscall_dispatch(DAWN_SYS_WRITE,DAWN_STDOUT_FD,0ULL,1ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EFAULT)
        return false;
    if (syscall_dispatch(DAWN_SYS_WRITE,DAWN_STDOUT_FD,UINT64_MAX,1ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EFAULT)
        return false;
    if (syscall_dispatch(DAWN_SYS_WRITE,DAWN_STDOUT_FD,0ULL,
                         DAWN_SYSCALL_MAX_WRITE+1ULL,0ULL,0ULL)!=
                         -(int64_t)DAWN_ERRNO_EINVAL) return false;
    if (syscall_dispatch(DAWN_SYS_EXIT,7ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    return syscall_dispatch(UINT64_MAX,0ULL,0ULL,0ULL,
                            0ULL,0ULL,0ULL)==-(int64_t)DAWN_ERRNO_ENOSYS;
}
#endif
