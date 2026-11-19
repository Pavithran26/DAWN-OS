/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_MUSL_COMPAT_H
#define DAWN_MUSL_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int64_t  ssize_t;
typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef int64_t  off_t;
typedef int64_t  time_t;

struct dawn_posix_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    mode_t   st_mode;
    uint32_t st_nlink;
    uid_t    st_uid;
    gid_t    st_gid;
    uint64_t st_rdev;
    off_t    st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    time_t   st_atime;
    time_t   st_mtime;
    time_t   st_ctime;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFLNK  0120000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

#endif
