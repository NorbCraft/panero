/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Experimental Windows (MinGW-w64) build: POSIX names that the Krusader code base
// uses but MinGW does not provide. CMake force-includes this header on Windows
// only. Windows has no Unix owners, groups or symbolic-link mode bits, so the
// fallbacks describe "a normal user, no special bits".

#ifndef PANERO_WIN32_COMPAT_H
#define PANERO_WIN32_COMPAT_H

#ifdef _WIN32

#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <unistd.h>

typedef int uid_t;
typedef int gid_t;

inline uid_t getuid() { return 1000; }
inline uid_t geteuid() { return 1000; }
inline gid_t getgid() { return 1000; }
inline gid_t getegid() { return 1000; }
inline int getgroups(int, gid_t *) { return 0; }
inline int lstat(const char *path, struct stat *buf) { return stat(path, buf); }
#endif

#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & 0170000) == S_IFLNK)
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & 0170000) == S_IFSOCK)
#endif
#ifndef S_ISUID
#define S_ISUID 04000
#endif
#ifndef S_ISGID
#define S_ISGID 02000
#endif
#ifndef S_ISVTX
#define S_ISVTX 01000
#endif
#ifndef S_IRWXG
#define S_IRWXG 00070
#endif
#ifndef S_IRGRP
#define S_IRGRP 00040
#endif
#ifndef S_IWGRP
#define S_IWGRP 00020
#endif
#ifndef S_IXGRP
#define S_IXGRP 00010
#endif
#ifndef S_IRWXO
#define S_IRWXO 00007
#endif
#ifndef S_IROTH
#define S_IROTH 00004
#endif
#ifndef S_IWOTH
#define S_IWOTH 00002
#endif
#ifndef S_IXOTH
#define S_IXOTH 00001
#endif

#endif // _WIN32
#endif // PANERO_WIN32_COMPAT_H
