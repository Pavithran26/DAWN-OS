/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syscall.h"
#include "page_table.h"
#include "process.h"
#include "vfs.h"

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
    const uint64_t active_pid=process_active_id();
    struct dawn_fd_entry fd_entry={0};

    if (descriptor>=(uint64_t)DAWN_PROCESS_MAX_FDS)
        return -(int64_t)DAWN_ERRNO_EBADF;

    if (active_pid==0ULL) {
        if (descriptor!=DAWN_STDOUT_FD && descriptor!=DAWN_STDERR_FD)
            return -(int64_t)DAWN_ERRNO_EBADF;
    } else {
        if (!process_fd_get(active_pid,(int)descriptor,&fd_entry))
            return -(int64_t)DAWN_ERRNO_EBADF;
        if ((fd_entry.flags&DAWN_O_WRONLY)==0U && (fd_entry.flags&DAWN_O_RDWR)==0U)
            return -(int64_t)DAWN_ERRNO_EBADF;
    }

    if (count>DAWN_SYSCALL_MAX_WRITE)
        return -(int64_t)DAWN_ERRNO_EINVAL;
    if (count==0ULL) return 0;
    /* Validate the entire range before producing any externally visible output. */
    if (!vm_user_range_valid(user_buffer,count,false))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    if (active_pid!=0ULL && fd_entry.type==DAWN_FD_TYPE_NULL)
        return (int64_t)count;

    if (active_pid!=0ULL && fd_entry.type==DAWN_FD_TYPE_VFS && fd_entry.node!=NULL) {
        struct vfs_node *vnode = (struct vfs_node *)fd_entry.node;
        while (written < count) {
            const uint64_t remaining = count - written;
            const size_t chunk = (remaining > (uint64_t)sizeof(temporary))
                                   ? sizeof(temporary) : (size_t)remaining;
            if (!vm_copy_from_user(temporary, user_buffer + written, chunk))
                return written != 0ULL ? (int64_t)written : -(int64_t)DAWN_ERRNO_EFAULT;
            int64_t res = vfs_write(vnode, fd_entry.offset + written, chunk, temporary);
            if (res <= 0) break;
            written += (uint64_t)res;
            if ((size_t)res < chunk) break;
        }
        fd_entry.offset += written;
        process_fd_set(active_pid, (int)descriptor, &fd_entry);
        return (int64_t)written;
    }

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

static int64_t syscall_read(uint64_t descriptor,uint64_t user_buffer,
                            uint64_t count) {
    uint8_t temporary[SYSCALL_WRITE_CHUNK];
    uint64_t read_bytes=0ULL;
    const uint64_t active_pid=process_active_id();
    struct dawn_fd_entry fd_entry={0};

    if (descriptor>=(uint64_t)DAWN_PROCESS_MAX_FDS)
        return -(int64_t)DAWN_ERRNO_EBADF;

    if (active_pid==0ULL) {
        if (descriptor!=0ULL)
            return -(int64_t)DAWN_ERRNO_EBADF;
    } else {
        if (!process_fd_get(active_pid,(int)descriptor,&fd_entry))
            return -(int64_t)DAWN_ERRNO_EBADF;
        if ((fd_entry.flags&DAWN_O_RDONLY)==0U && (fd_entry.flags&DAWN_O_RDWR)==0U)
            return -(int64_t)DAWN_ERRNO_EBADF;
    }

    if (count>DAWN_SYSCALL_MAX_WRITE)
        return -(int64_t)DAWN_ERRNO_EINVAL;
    if (count==0ULL) return 0;
    if (!vm_user_range_valid(user_buffer,count,true))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    if (active_pid!=0ULL && fd_entry.type==DAWN_FD_TYPE_NULL)
        return 0;

    if (active_pid!=0ULL && fd_entry.type==DAWN_FD_TYPE_VFS && fd_entry.node!=NULL) {
        struct vfs_node *vnode = (struct vfs_node *)fd_entry.node;
        while (read_bytes < count) {
            const uint64_t remaining = count - read_bytes;
            const size_t chunk = (remaining > (uint64_t)sizeof(temporary))
                                   ? sizeof(temporary) : (size_t)remaining;
            int64_t res = vfs_read(vnode, fd_entry.offset + read_bytes, chunk, temporary);
            if (res <= 0) break;
            if (!vm_copy_to_user(user_buffer + read_bytes, temporary, (size_t)res))
                return read_bytes != 0ULL ? (int64_t)read_bytes : -(int64_t)DAWN_ERRNO_EFAULT;
            read_bytes += (uint64_t)res;
            if ((size_t)res < chunk) break;
        }
        fd_entry.offset += read_bytes;
        process_fd_set(active_pid, (int)descriptor, &fd_entry);
        return (int64_t)read_bytes;
    }

    while (read_bytes<count) {
        if ((inb((uint16_t)(SERIAL_COM1+5U))&0x01U)==0U) {
            break;
        }
        temporary[read_bytes%sizeof(temporary)]=inb((uint16_t)SERIAL_COM1);
        ++read_bytes;
        if ((read_bytes%sizeof(temporary))==0U || read_bytes==count) {
            const size_t chunk=((read_bytes-1ULL)%sizeof(temporary))+1U;
            const uint64_t offset=read_bytes-chunk;
            if (!vm_copy_to_user(user_buffer+offset,temporary,chunk))
                return -(int64_t)DAWN_ERRNO_EFAULT;
        }
    }
    return (int64_t)read_bytes;
}

static int64_t syscall_close(uint64_t descriptor) {
    const uint64_t active_pid=process_active_id();
    if (active_pid==0ULL || descriptor>=(uint64_t)DAWN_PROCESS_MAX_FDS)
        return -(int64_t)DAWN_ERRNO_EBADF;
    return process_fd_close(active_pid,(int)descriptor) ? 0 : -(int64_t)DAWN_ERRNO_EBADF;
}

static int64_t syscall_dup(uint64_t oldfd) {
    const uint64_t active_pid=process_active_id();
    int newfd=-1;
    if (active_pid==0ULL || oldfd>=(uint64_t)DAWN_PROCESS_MAX_FDS)
        return -(int64_t)DAWN_ERRNO_EBADF;
    return process_fd_dup(active_pid,(int)oldfd,&newfd) ? (int64_t)newfd : -(int64_t)DAWN_ERRNO_EBADF;
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
        case DAWN_SYS_READ:
            return syscall_read(argument0,argument1,argument2);
        case DAWN_SYS_CLOSE:
            return syscall_close(argument0);
        case DAWN_SYS_DUP:
            return syscall_dup(argument0);
        case DAWN_SYS_GETPID:
            return (int64_t)process_active_id();
        case DAWN_SYS_EXIT:
        case DAWN_SYS_YIELD:
        case DAWN_SYS_WAIT:
        case DAWN_SYS_SPAWN:
        case DAWN_SYS_EXEC:
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
                         DAWN_SYSCALL_MAX_WRITE+1ULL,0ULL,0ULL,0ULL)!=
                         -(int64_t)DAWN_ERRNO_EINVAL) return false;
    if (syscall_dispatch(DAWN_SYS_EXIT,7ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    if (syscall_dispatch(DAWN_SYS_SPAWN,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    if (syscall_dispatch(DAWN_SYS_EXEC,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_ENOSYS)
        return false;
    if (syscall_dispatch(DAWN_SYS_CLOSE,100ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    if (syscall_dispatch(DAWN_SYS_DUP,100ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    if (syscall_dispatch(DAWN_SYS_READ,100ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    return syscall_dispatch(UINT64_MAX,0ULL,0ULL,0ULL,
                            0ULL,0ULL,0ULL)==-(int64_t)DAWN_ERRNO_ENOSYS;
}
#endif
