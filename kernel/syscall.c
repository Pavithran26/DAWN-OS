/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syscall.h"
#include "page_table.h"
#include "process.h"
#include "vfs.h"
#include "rtc.h"
#include "system_info.h"

#define SERIAL_COM1 0x3f8U
#define SERIAL_POLL_LIMIT 100000U
#define SYSCALL_WRITE_CHUNK 128U

#define MSR_EFER  0xC0000080U
#define MSR_STAR  0xC0000081U
#define MSR_LSTAR 0xC0000082U
#define MSR_FMASK 0xC0000084U

#define EFER_SCE      (1ULL << 0)
#define FMASK_SYSCALL 0x0700ULL /* Mask IF (bit 9), TF (bit 8), DF (bit 10) */

extern void dawn_syscall_entry(void);

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFFULL);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low = 0, high = 0;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

bool syscall_init_hardware(void) {
    /* 1. Enable System Call Extensions (SCE) in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    /* 2. Configure STAR:
     * User base = 0x0010 (User SS = 0x18 | 3 = 0x1B, User CS = 0x20 | 3 = 0x23)
     * Kernel base = 0x0008 (Kernel CS = 0x08, Kernel SS = 0x10)
     */
    uint64_t star = ((uint64_t)0x0010ULL << 48) | ((uint64_t)0x0008ULL << 32);
    wrmsr(MSR_STAR, star);

    /* 3. Configure LSTAR with kernel fast syscall entry point */
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)&dawn_syscall_entry);

    /* 4. Configure FMASK to clear IF on syscall entry */
    wrmsr(MSR_FMASK, FMASK_SYSCALL);

    return true;
}

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

static int64_t syscall_dup2(uint64_t oldfd, uint64_t newfd) {
    const uint64_t active_pid=process_active_id();
    if (active_pid==0ULL || oldfd>=(uint64_t)DAWN_PROCESS_MAX_FDS ||
        newfd>=(uint64_t)DAWN_PROCESS_MAX_FDS)
        return -(int64_t)DAWN_ERRNO_EBADF;
    return process_fd_dup2(active_pid,(int)oldfd,(int)newfd) ? (int64_t)newfd : -(int64_t)DAWN_ERRNO_EBADF;
}

static int64_t syscall_open(uint64_t user_path, uint64_t flags, uint64_t mode) {
    (void)mode;
    char path_buf[VFS_MAX_PATH];
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (user_path == 0ULL || !vm_user_range_valid(user_path, 1, false))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    size_t i = 0;
    while (i + 1 < sizeof(path_buf)) {
        uint8_t ch = 0;
        if (!vm_copy_from_user(&ch, user_path + i, 1))
            return -(int64_t)DAWN_ERRNO_EFAULT;
        path_buf[i] = (char)ch;
        if (ch == '\0') break;
        i++;
    }
    path_buf[i] = '\0';

    struct vfs_node *node = NULL;
    if (!vfs_resolve_path(path_buf, &node) || node == NULL) {
        if ((flags & 0x0040U) != 0U) { /* DAWN_O_CREAT */
            if (vfs_create(path_buf, VFS_FLAG_READ | VFS_FLAG_WRITE, &node) != 0 || node == NULL) {
                return -(int64_t)DAWN_ERRNO_ENOENT;
            }
        } else {
            return -(int64_t)DAWN_ERRNO_ENOENT;
        }
    }

    uint32_t fd_flags = 0;
    if ((flags & 0x03) == 0) fd_flags = DAWN_O_RDONLY;
    else if ((flags & 0x03) == 1) fd_flags = DAWN_O_WRONLY;
    else fd_flags = DAWN_O_RDWR;

    int new_fd = -1;
    if (!process_fd_allocate(active_pid, DAWN_FD_TYPE_VFS, fd_flags, node, &new_fd)) {
        return -(int64_t)DAWN_ERRNO_EMFILE;
    }
    return (int64_t)new_fd;
}

static int64_t syscall_brk(uint64_t new_brk) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    return (int64_t)process_brk(active_pid, new_brk);
}

static uint64_t rtc_to_unix_epoch(const struct rtc_datetime *dt) {
    if (dt == NULL || dt->year < 1970) return 0;
    static const uint16_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    uint64_t days = 0;
    for (uint32_t y = 1970; y < dt->year; y++) {
        days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
    }
    if (dt->month >= 1 && dt->month <= 12) {
        days += days_before_month[dt->month - 1];
        if (dt->month > 2 && ((dt->year % 4 == 0 && dt->year % 100 != 0) || (dt->year % 400 == 0))) {
            days += 1;
        }
    }
    if (dt->day >= 1) days += (dt->day - 1);
    uint64_t seconds = days * 86400ULL + (uint64_t)dt->hour * 3600ULL +
                       (uint64_t)dt->minute * 60ULL + (uint64_t)dt->second;
    return seconds;
}

static int64_t syscall_time(uint64_t user_tloc) {
    struct rtc_datetime dt = {0};
    uint64_t timestamp = 0;
    if (rtc_get_datetime(&dt)) {
        timestamp = rtc_to_unix_epoch(&dt);
    }
    if (user_tloc != 0ULL) {
        if (!vm_user_range_valid(user_tloc, sizeof(uint64_t), true))
            return -(int64_t)DAWN_ERRNO_EFAULT;
        if (!vm_copy_to_user(user_tloc, &timestamp, sizeof(uint64_t)))
            return -(int64_t)DAWN_ERRNO_EFAULT;
    }
    return (int64_t)timestamp;
}

static int64_t syscall_getcwd(uint64_t user_buf, uint64_t size) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (user_buf == 0ULL || size == 0ULL) return -(int64_t)DAWN_ERRNO_EINVAL;
    if (!vm_user_range_valid(user_buf, size, true)) return -(int64_t)DAWN_ERRNO_EFAULT;

    char cwd[VFS_MAX_PATH] = {0};
    if (!process_getcwd(active_pid, cwd, sizeof(cwd))) return -(int64_t)DAWN_ERRNO_EINVAL;

    size_t len = 0;
    while (cwd[len] != '\0') len++;
    if (len + 1 > size) return -(int64_t)DAWN_ERRNO_ENAMETOOLONG;

    if (!vm_copy_to_user(user_buf, cwd, len + 1)) return -(int64_t)DAWN_ERRNO_EFAULT;
    return (int64_t)user_buf;
}

static int64_t syscall_chdir(uint64_t user_path) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (user_path == 0ULL || !vm_user_range_valid(user_path, 1, false))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    char path[VFS_MAX_PATH] = {0};
    size_t i = 0;
    while (i + 1 < sizeof(path)) {
        uint8_t ch = 0;
        if (!vm_copy_from_user(&ch, user_path + i, 1)) return -(int64_t)DAWN_ERRNO_EFAULT;
        path[i] = (char)ch;
        if (ch == '\0') break;
        i++;
    }
    path[i] = '\0';

    struct vfs_node *node = NULL;
    if (!vfs_resolve_path(path, &node) || node == NULL || node->type != VFS_TYPE_DIRECTORY)
        return -(int64_t)DAWN_ERRNO_ENOENT;

    if (!process_chdir(active_pid, path)) return -(int64_t)DAWN_ERRNO_EINVAL;
    return 0;
}

static int64_t syscall_unlink(uint64_t user_path) {
    if (user_path == 0ULL || !vm_user_range_valid(user_path, 1, false))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    char path[VFS_MAX_PATH] = {0};
    size_t i = 0;
    while (i + 1 < sizeof(path)) {
        uint8_t ch = 0;
        if (!vm_copy_from_user(&ch, user_path + i, 1)) return -(int64_t)DAWN_ERRNO_EFAULT;
        path[i] = (char)ch;
        if (ch == '\0') break;
        i++;
    }
    path[i] = '\0';

    int res = vfs_unlink(path);
    return res == 0 ? 0 : -(int64_t)DAWN_ERRNO_ENOENT;
}

static int64_t syscall_mkdir(uint64_t user_path, uint64_t mode) {
    if (user_path == 0ULL || !vm_user_range_valid(user_path, 1, false))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    char path[VFS_MAX_PATH] = {0};
    size_t i = 0;
    while (i + 1 < sizeof(path)) {
        uint8_t ch = 0;
        if (!vm_copy_from_user(&ch, user_path + i, 1)) return -(int64_t)DAWN_ERRNO_EFAULT;
        path[i] = (char)ch;
        if (ch == '\0') break;
        i++;
    }
    path[i] = '\0';

    int res = vfs_mkdir(path, (uint32_t)mode, NULL);
    return res == 0 ? 0 : -(int64_t)DAWN_ERRNO_EIO;
}

struct dawn_stat_buf {
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

static int64_t syscall_stat(uint64_t user_path, uint64_t user_buf) {
    if (user_path == 0ULL || !vm_user_range_valid(user_path, 1, false))
        return -(int64_t)DAWN_ERRNO_EFAULT;
    if (user_buf == 0ULL || !vm_user_range_valid(user_buf, sizeof(struct dawn_stat_buf), true))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    char path[VFS_MAX_PATH] = {0};
    size_t i = 0;
    while (i + 1 < sizeof(path)) {
        uint8_t ch = 0;
        if (!vm_copy_from_user(&ch, user_path + i, 1)) return -(int64_t)DAWN_ERRNO_EFAULT;
        path[i] = (char)ch;
        if (ch == '\0') break;
        i++;
    }
    path[i] = '\0';

    struct vfs_node *node = NULL;
    if (!vfs_resolve_path(path, &node) || node == NULL)
        return -(int64_t)DAWN_ERRNO_ENOENT;

    struct dawn_stat_buf st = {0};
    st.st_ino = node->inode;
    st.st_mode = (node->type == VFS_TYPE_DIRECTORY) ? 0040755U : 0100644U;
    st.st_nlink = 1;
    st.st_size = (int64_t)node->size;
    st.st_blksize = 4096;
    st.st_blocks = (int64_t)((node->size + 511) / 512);

    if (!vm_copy_to_user(user_buf, &st, sizeof(st)))
        return -(int64_t)DAWN_ERRNO_EFAULT;
    return 0;
}

static int64_t syscall_lseek(uint64_t descriptor, int64_t offset, uint64_t whence) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (descriptor >= (uint64_t)DAWN_PROCESS_MAX_FDS) return -(int64_t)DAWN_ERRNO_EBADF;

    struct dawn_fd_entry entry = {0};
    if (!process_fd_get(active_pid, (int)descriptor, &entry) || entry.type != DAWN_FD_TYPE_VFS)
        return -(int64_t)DAWN_ERRNO_EBADF;

    struct vfs_node *node = (struct vfs_node *)entry.node;
    if (node == NULL) return -(int64_t)DAWN_ERRNO_EBADF;

    int64_t new_offset = 0;
    if (whence == 0) {
        new_offset = offset;
    } else if (whence == 1) {
        new_offset = (int64_t)entry.offset + offset;
    } else if (whence == 2) {
        new_offset = (int64_t)node->size + offset;
    } else {
        return -(int64_t)DAWN_ERRNO_EINVAL;
    }

    if (new_offset < 0) return -(int64_t)DAWN_ERRNO_EINVAL;
    entry.offset = (uint64_t)new_offset;
    (void)process_fd_set(active_pid, (int)descriptor, &entry);
    return new_offset;
}

static int64_t syscall_getdents(uint64_t descriptor, uint64_t user_buf, uint64_t count) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (descriptor >= (uint64_t)DAWN_PROCESS_MAX_FDS) return -(int64_t)DAWN_ERRNO_EBADF;
    if (user_buf == 0ULL || count < sizeof(struct dawn_dirent) ||
        !vm_user_range_valid(user_buf, sizeof(struct dawn_dirent), true))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    struct dawn_fd_entry entry = {0};
    if (!process_fd_get(active_pid, (int)descriptor, &entry) || entry.type != DAWN_FD_TYPE_VFS)
        return -(int64_t)DAWN_ERRNO_EBADF;

    struct vfs_node *node = (struct vfs_node *)entry.node;
    if (node == NULL || node->type != VFS_TYPE_DIRECTORY) return -(int64_t)DAWN_ERRNO_EBADF;

    uint32_t dir_index = (uint32_t)entry.offset;
    struct vfs_dirent ventry = {0};
    int res = vfs_readdir(node, dir_index, &ventry);
    if (res != 0) return 0;

    struct dawn_dirent dent = {0};
    dent.d_ino = ventry.inode;
    dent.d_type = (uint8_t)ventry.type;
    dent.d_size = ventry.size;
    for (size_t k = 0; k < sizeof(dent.d_name) && ventry.name[k] != '\0'; k++) {
        dent.d_name[k] = ventry.name[k];
    }

    if (!vm_copy_to_user(user_buf, &dent, sizeof(dent)))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    entry.offset++;
    (void)process_fd_set(active_pid, (int)descriptor, &entry);
    return (int64_t)sizeof(dent);
}

static int64_t syscall_kill(uint64_t target_pid, uint64_t sig) {
    const uint64_t active_pid = process_active_id();
    if (active_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EPERM;
    if (target_pid == 0ULL) return -(int64_t)DAWN_ERRNO_EINVAL;

    struct process_info info = {0};
    if (!process_get_info(target_pid, &info))
        return -(int64_t)DAWN_ERRNO_ESRCH;

    if (sig == 9 || sig == 15) {
        (void)process_mark_exited(target_pid);
        return 0;
    }
    return 0;
}

static int64_t syscall_sysinfo(uint64_t user_buf) {
    if (user_buf == 0ULL || !vm_user_range_valid(user_buf, sizeof(struct dawn_sysinfo_user), true))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    struct dawn_system_info kern_info = {0};
    if (!dawn_system_info_refresh(&kern_info))
        return -(int64_t)DAWN_ERRNO_EIO;

    struct dawn_sysinfo_user uinfo = {0};
    uinfo.cpu_count = kern_info.cpu_count;
    uinfo.online_cpu_count = kern_info.online_cpu_count;
    uinfo.total_memory_bytes = kern_info.physical_memory_bytes;
    uinfo.free_memory_bytes = kern_info.free_memory_bytes;
    uinfo.uptime_seconds = (uint64_t)kern_info.uptime_seconds;

    size_t i = 0;
    while (kern_info.cpu_model[i] != '\0' && i + 1 < sizeof(uinfo.cpu_model)) {
        uinfo.cpu_model[i] = kern_info.cpu_model[i];
        i++;
    }
    uinfo.cpu_model[i] = '\0';

    size_t j = 0;
    if (kern_info.boot_mode != NULL) {
        while (kern_info.boot_mode[j] != '\0' && j + 1 < sizeof(uinfo.boot_mode)) {
            uinfo.boot_mode[j] = kern_info.boot_mode[j];
            j++;
        }
    }
    uinfo.boot_mode[j] = '\0';

    if (!vm_copy_to_user(user_buf, &uinfo, sizeof(uinfo)))
        return -(int64_t)DAWN_ERRNO_EFAULT;

    return 0;
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
        case DAWN_SYS_DUP2:
            return syscall_dup2(argument0,argument1);
        case DAWN_SYS_GETPID:
            return (int64_t)process_active_id();
        case DAWN_SYS_OPEN:
            return syscall_open(argument0,argument1,argument2);
        case DAWN_SYS_BRK:
            return syscall_brk(argument0);
        case DAWN_SYS_TIME:
            return syscall_time(argument0);
        case DAWN_SYS_GETCWD:
            return syscall_getcwd(argument0,argument1);
        case DAWN_SYS_CHDIR:
            return syscall_chdir(argument0);
        case DAWN_SYS_UNLINK:
            return syscall_unlink(argument0);
        case DAWN_SYS_MKDIR:
            return syscall_mkdir(argument0,argument1);
        case DAWN_SYS_STAT:
            return syscall_stat(argument0,argument1);
        case DAWN_SYS_LSEEK:
            return syscall_lseek(argument0,(int64_t)argument1,argument2);
        case DAWN_SYS_GETDENTS:
            return syscall_getdents(argument0,argument1,argument2);
        case DAWN_SYS_KILL:
            return syscall_kill(argument0,argument1);
        case DAWN_SYS_SYSINFO:
            return syscall_sysinfo(argument0);
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
    if (!syscall_init_hardware()) return false;
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
    if (syscall_dispatch(DAWN_SYS_DUP2,100ULL,101ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    if (syscall_dispatch(DAWN_SYS_READ,100ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EBADF)
        return false;
    if (syscall_dispatch(DAWN_SYS_OPEN,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_BRK,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_GETCWD,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_CHDIR,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_TIME,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)<0)
        return false;
    if (syscall_dispatch(DAWN_SYS_MKDIR,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EFAULT)
        return false;
    if (syscall_dispatch(DAWN_SYS_STAT,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EFAULT)
        return false;
    if (syscall_dispatch(DAWN_SYS_LSEEK,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_GETDENTS,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_KILL,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EPERM)
        return false;
    if (syscall_dispatch(DAWN_SYS_SYSINFO,0ULL,0ULL,0ULL,
                         0ULL,0ULL,0ULL)!=-(int64_t)DAWN_ERRNO_EFAULT)
        return false;
    return syscall_dispatch(UINT64_MAX,0ULL,0ULL,0ULL,
                            0ULL,0ULL,0ULL)==-(int64_t)DAWN_ERRNO_ENOSYS;
}
#endif
