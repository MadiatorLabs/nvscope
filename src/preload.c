#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "env.h"
#include "fd_map.h"
#include "ioctl_filter.h"
#include "ioctl_trace.h"
#include "log.h"
#include "proc_filter.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static struct nvfix_config g_nvfix_config;
static struct nvfix_allowed_gpus g_allowed_gpus;
static int g_proc_filter_ready;

static int (*g_real_open)(const char *, int, ...);
static int (*g_real_open64)(const char *, int, ...);
static int (*g_real_openat)(int, const char *, int, ...);
static int (*g_real_openat64)(int, const char *, int, ...);
static int (*g_real_close)(int);
static ssize_t (*g_real_read)(int, void *, size_t);
static int (*g_real_stat)(const char *, struct stat *);
static int (*g_real_fstat)(int, struct stat *);
static int (*g_real_newfstatat)(int, const char *, struct stat *, int);
static DIR *(*g_real_opendir)(const char *);
static DIR *(*g_real_opendir64)(const char *);
static DIR *(*g_real_fdopendir)(int);
static struct dirent *(*g_real_readdir)(DIR *);
static struct dirent64 *(*g_real_readdir64)(DIR *);

static void *nvfix_dlsym_next(const char *name) {
    return dlsym(RTLD_NEXT, name);
}

static int nvfix_fail_enosys(void) {
    errno = ENOSYS;
    return -1;
}

static int nvfix_has_mode_arg(int flags) {
    return (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
}

static int nvfix_path_is_absolute(const char *path) {
    return path != NULL && path[0] == '/';
}

static int nvfix_path_is_relative(const char *path) {
    return path != NULL && path[0] != '/';
}

static int nvfix_proc_denied(const char *path) {
    return g_proc_filter_ready &&
           nvfix_path_is_absolute(path) &&
           nvfix_proc_path_allowed(&g_allowed_gpus, path) == NVFIX_PROC_DENY;
}

static int nvfix_open_denied_path(const char *path, const struct nvfix_fd_record *record) {
    if (record != NULL && record->kind != NVFIX_FD_OTHER) {
        nvfix_log_trace(&g_nvfix_config, "open %s -> fd -1", path);
    }

    errno = ENOENT;
    return -1;
}

static void nvfix_trace_and_track_open(const char *path, int fd) {
    struct nvfix_fd_record record;

    if (!nvfix_classify_path(path, &record)) {
        return;
    }

    nvfix_log_trace(&g_nvfix_config, "open %s -> fd %d", path, fd);
    if (fd >= 0) {
        nvfix_fd_map_set(fd, &record);
    }
}

static int nvfix_normalize_relative_path(const char *path, char *buffer, size_t buffer_len) {
    size_t read_offset;
    size_t write_offset;

    if (path == NULL || buffer == NULL || buffer_len == 0 || path[0] == '/') {
        return 0;
    }

    read_offset = 0;
    write_offset = 0;
    while (path[read_offset] != '\0') {
        const char *segment;
        size_t segment_len;

        while (path[read_offset] == '/') {
            read_offset++;
        }
        if (path[read_offset] == '\0') {
            break;
        }

        segment = path + read_offset;
        segment_len = 0;
        while (segment[segment_len] != '\0' && segment[segment_len] != '/') {
            segment_len++;
        }

        if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
            return 0;
        }

        if (!(segment_len == 1 && segment[0] == '.')) {
            if (write_offset != 0) {
                if (write_offset + 1 >= buffer_len) {
                    return 0;
                }
                buffer[write_offset] = '/';
                write_offset++;
            }
            if (write_offset + segment_len >= buffer_len) {
                return 0;
            }
            memcpy(buffer + write_offset, segment, segment_len);
            write_offset += segment_len;
        }

        read_offset += segment_len;
    }

    if (write_offset == 0 || write_offset >= buffer_len) {
        return 0;
    }

    buffer[write_offset] = '\0';
    return 1;
}

static const char *nvfix_effective_openat_path(int dirfd,
                                               const char *path,
                                               char *buffer,
                                               size_t buffer_len) {
    struct nvfix_fd_record record;
    char normalized[NVFIX_PATH_MAX];
    const char *relative_path;
    int written;

    if (!nvfix_path_is_relative(path) ||
        buffer == NULL ||
        buffer_len == 0 ||
        !nvfix_fd_map_get(dirfd, &record) ||
        record.kind != NVFIX_FD_PROC_GPUS_DIR) {
        return path;
    }

    if (!nvfix_normalize_relative_path(path, normalized, sizeof(normalized))) {
        return path;
    }

    relative_path = normalized;
    written = snprintf(buffer, buffer_len, "/proc/driver/nvidia/gpus/%s", relative_path);
    if (written < 0 || (size_t)written >= buffer_len) {
        return path;
    }

    return buffer;
}

static int nvfix_readdir_entry_allowed(int dirfd, const char *name) {
    struct nvfix_fd_record record;

    if (!g_proc_filter_ready ||
        name == NULL ||
        !nvfix_fd_map_get(dirfd, &record) ||
        record.kind != NVFIX_FD_PROC_GPUS_DIR ||
        strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        return 1;
    }

    return nvfix_allowed_contains_bus(&g_allowed_gpus, name);
}

#ifdef NVFIX_TESTING
const char *nvfix_test_effective_openat_path(int dirfd,
                                             const char *path,
                                             char *buffer,
                                             size_t buffer_len) {
    return nvfix_effective_openat_path(dirfd, path, buffer, buffer_len);
}

int nvfix_test_readdir_entry_allowed(int dirfd, const char *name) {
    return nvfix_readdir_entry_allowed(dirfd, name);
}

void nvfix_test_set_proc_filter_ready(int ready) {
    g_proc_filter_ready = ready;
}

void nvfix_test_set_allowed_bus(const char *bus_id) {
    memset(&g_allowed_gpus, 0, sizeof(g_allowed_gpus));
    g_allowed_gpus.valid = 1;
    g_allowed_gpus.count = 1;
    if (bus_id != NULL) {
        strncpy(g_allowed_gpus.gpus[0].bus_id, bus_id, sizeof(g_allowed_gpus.gpus[0].bus_id) - 1);
        g_allowed_gpus.gpus[0].bus_id[sizeof(g_allowed_gpus.gpus[0].bus_id) - 1] = '\0';
    }
}
#endif

static int nvfix_open_common(const char *path, int flags, mode_t mode, int has_mode, int use_open64) {
    int fd;
    struct nvfix_fd_record record;
    int classified;

    classified = nvfix_classify_path(path, &record);
    if (nvfix_proc_denied(path)) {
        return nvfix_open_denied_path(path, classified ? &record : NULL);
    }

    if (use_open64) {
        if (g_real_open64 == NULL) {
            g_real_open64 = (int (*)(const char *, int, ...))nvfix_dlsym_next("open64");
        }
        if (g_real_open64 != NULL) {
            fd = has_mode ? g_real_open64(path, flags, mode) : g_real_open64(path, flags);
        } else {
            if (g_real_open == NULL) {
                g_real_open = (int (*)(const char *, int, ...))nvfix_dlsym_next("open");
            }
            if (g_real_open == NULL) {
                return nvfix_fail_enosys();
            }
            fd = has_mode ? g_real_open(path, flags, mode) : g_real_open(path, flags);
        }
    } else {
        if (g_real_open == NULL) {
            g_real_open = (int (*)(const char *, int, ...))nvfix_dlsym_next("open");
        }
        if (g_real_open == NULL) {
            return nvfix_fail_enosys();
        }
        fd = has_mode ? g_real_open(path, flags, mode) : g_real_open(path, flags);
    }

    if (classified) {
        nvfix_log_trace(&g_nvfix_config, "open %s -> fd %d", path, fd);
        if (fd >= 0) {
            nvfix_fd_map_set(fd, &record);
        }
    }

    return fd;
}

static int nvfix_openat_common(int dirfd,
                               const char *path,
                               int flags,
                               mode_t mode,
                               int has_mode,
                               int use_open64) {
    int fd;
    char effective_buffer[NVFIX_PATH_MAX];
    const char *effective_path;

    effective_path = nvfix_effective_openat_path(dirfd, path, effective_buffer, sizeof(effective_buffer));
    if (nvfix_proc_denied(effective_path)) {
        struct nvfix_fd_record record;
        int classified = nvfix_classify_path(effective_path, &record);
        return nvfix_open_denied_path(effective_path, classified ? &record : NULL);
    }

    if (use_open64) {
        if (g_real_openat64 == NULL) {
            g_real_openat64 = (int (*)(int, const char *, int, ...))nvfix_dlsym_next("openat64");
        }
        if (g_real_openat64 != NULL) {
            fd = has_mode ? g_real_openat64(dirfd, path, flags, mode) : g_real_openat64(dirfd, path, flags);
        } else {
            if (g_real_openat == NULL) {
                g_real_openat = (int (*)(int, const char *, int, ...))nvfix_dlsym_next("openat");
            }
            if (g_real_openat == NULL) {
                return nvfix_fail_enosys();
            }
            fd = has_mode ? g_real_openat(dirfd, path, flags, mode) : g_real_openat(dirfd, path, flags);
        }
    } else {
        if (g_real_openat == NULL) {
            g_real_openat = (int (*)(int, const char *, int, ...))nvfix_dlsym_next("openat");
        }
        if (g_real_openat == NULL) {
            return nvfix_fail_enosys();
        }
        fd = has_mode ? g_real_openat(dirfd, path, flags, mode) : g_real_openat(dirfd, path, flags);
    }

    if (nvfix_path_is_absolute(effective_path) || dirfd == AT_FDCWD) {
        nvfix_trace_and_track_open(effective_path, fd);
    }

    return fd;
}

int open(const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0;
    int has_mode;

    has_mode = nvfix_has_mode_arg(flags);
    if (has_mode) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    return nvfix_open_common(path, flags, mode, has_mode, 0);
}

int open64(const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0;
    int has_mode;

    has_mode = nvfix_has_mode_arg(flags);
    if (has_mode) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    return nvfix_open_common(path, flags, mode, has_mode, 1);
}

int openat(int dirfd, const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0;
    int has_mode;

    has_mode = nvfix_has_mode_arg(flags);
    if (has_mode) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    return nvfix_openat_common(dirfd, path, flags, mode, has_mode, 0);
}

int openat64(int dirfd, const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0;
    int has_mode;

    has_mode = nvfix_has_mode_arg(flags);
    if (has_mode) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    return nvfix_openat_common(dirfd, path, flags, mode, has_mode, 1);
}

int close(int fd) {
    int result;

    if (g_real_close == NULL) {
        g_real_close = (int (*)(int))nvfix_dlsym_next("close");
    }
    if (g_real_close == NULL) {
        return nvfix_fail_enosys();
    }

    result = g_real_close(fd);
    if (result == 0) {
        nvfix_fd_map_remove(fd);
    }

    return result;
}

ssize_t read(int fd, void *buf, size_t count) {
    if (g_real_read == NULL) {
        g_real_read = (ssize_t (*)(int, void *, size_t))nvfix_dlsym_next("read");
    }
    if (g_real_read == NULL) {
        return (ssize_t)nvfix_fail_enosys();
    }

    return g_real_read(fd, buf, count);
}

int stat(const char *path, struct stat *statbuf) {
    if (nvfix_proc_denied(path)) {
        errno = ENOENT;
        return -1;
    }

    if (g_real_stat == NULL) {
        g_real_stat = (int (*)(const char *, struct stat *))nvfix_dlsym_next("stat");
    }
    if (g_real_stat == NULL) {
        return nvfix_fail_enosys();
    }

    return g_real_stat(path, statbuf);
}

int fstat(int fd, struct stat *statbuf) {
    if (g_real_fstat == NULL) {
        g_real_fstat = (int (*)(int, struct stat *))nvfix_dlsym_next("fstat");
    }
    if (g_real_fstat == NULL) {
        return nvfix_fail_enosys();
    }

    return g_real_fstat(fd, statbuf);
}

int newfstatat(int dirfd, const char *path, struct stat *statbuf, int flags) {
    char effective_buffer[NVFIX_PATH_MAX];
    const char *effective_path;

    effective_path = nvfix_effective_openat_path(dirfd, path, effective_buffer, sizeof(effective_buffer));
    if (nvfix_proc_denied(effective_path)) {
        errno = ENOENT;
        return -1;
    }

    if (g_real_newfstatat == NULL) {
        g_real_newfstatat =
            (int (*)(int, const char *, struct stat *, int))nvfix_dlsym_next("newfstatat");
    }
    if (g_real_newfstatat == NULL) {
        return nvfix_fail_enosys();
    }

    return g_real_newfstatat(dirfd, path, statbuf, flags);
}

static DIR *nvfix_opendir_common(const char *path, int use_opendir64) {
    DIR *dir;

    if (nvfix_proc_denied(path)) {
        errno = ENOENT;
        return NULL;
    }

    if (use_opendir64) {
        if (g_real_opendir64 == NULL) {
            g_real_opendir64 = (DIR *(*)(const char *))nvfix_dlsym_next("opendir64");
        }
        if (g_real_opendir64 != NULL) {
            dir = g_real_opendir64(path);
        } else {
            if (g_real_opendir == NULL) {
                g_real_opendir = (DIR *(*)(const char *))nvfix_dlsym_next("opendir");
            }
            if (g_real_opendir == NULL) {
                errno = ENOSYS;
                return NULL;
            }
            dir = g_real_opendir(path);
        }
    } else {
        if (g_real_opendir == NULL) {
            g_real_opendir = (DIR *(*)(const char *))nvfix_dlsym_next("opendir");
        }
        if (g_real_opendir == NULL) {
            errno = ENOSYS;
            return NULL;
        }
        dir = g_real_opendir(path);
    }

    if (dir != NULL) {
        nvfix_trace_and_track_open(path, dirfd(dir));
    }

    return dir;
}

DIR *opendir(const char *path) {
    return nvfix_opendir_common(path, 0);
}

DIR *opendir64(const char *path) {
    return nvfix_opendir_common(path, 1);
}

DIR *fdopendir(int fd) {
    if (g_real_fdopendir == NULL) {
        g_real_fdopendir = (DIR *(*)(int))nvfix_dlsym_next("fdopendir");
    }
    if (g_real_fdopendir == NULL) {
        errno = ENOSYS;
        return NULL;
    }

    return g_real_fdopendir(fd);
}

struct dirent *readdir(DIR *dirp) {
    int fd;
    struct dirent *entry;

    if (g_real_readdir == NULL) {
        g_real_readdir = (struct dirent *(*)(DIR *))nvfix_dlsym_next("readdir");
    }
    if (g_real_readdir == NULL) {
        errno = ENOSYS;
        return NULL;
    }

    entry = g_real_readdir(dirp);
    if (entry == NULL) {
        return NULL;
    }

    fd = dirfd(dirp);
    while (!nvfix_readdir_entry_allowed(fd, entry->d_name)) {
        entry = g_real_readdir(dirp);
        if (entry == NULL) {
            return NULL;
        }
    }

    return entry;
}

struct dirent64 *readdir64(DIR *dirp) {
    int fd;
    struct dirent64 *entry;

    if (g_real_readdir64 == NULL) {
        g_real_readdir64 = (struct dirent64 *(*)(DIR *))nvfix_dlsym_next("readdir64");
    }
    if (g_real_readdir64 == NULL) {
        errno = ENOSYS;
        return NULL;
    }

    entry = g_real_readdir64(dirp);
    if (entry == NULL) {
        return NULL;
    }

    fd = dirfd(dirp);
    while (!nvfix_readdir_entry_allowed(fd, entry->d_name)) {
        entry = g_real_readdir64(dirp);
        if (entry == NULL) {
            return NULL;
        }
    }

    return entry;
}

ssize_t getdents64(int fd, void *dirp, size_t count) {
    ssize_t result;
    struct nvfix_fd_record record;

    result = (ssize_t)syscall(SYS_getdents64, fd, dirp, count);
    if (result > 0 &&
        g_proc_filter_ready &&
        nvfix_fd_map_get((int)fd, &record) &&
        record.kind == NVFIX_FD_PROC_GPUS_DIR) {
        result = nvfix_filter_dirent64_buffer((char *)dirp, result, &g_allowed_gpus);
    }

    return result;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    void *arg;
    int result;
    struct nvfix_fd_record record;

    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);

    result = (int)syscall(SYS_ioctl, fd, request, arg);
    if (nvfix_fd_map_get(fd, &record) && record.kind == NVFIX_FD_NVIDIA_CTL) {
        nvfix_ioctl_trace(&g_nvfix_config, fd, request, arg, result);
        nvfix_ioctl_filter_after(&g_nvfix_config,
                                 g_proc_filter_ready ? &g_allowed_gpus : NULL,
                                 request,
                                 arg,
                                 result);
    }

    return result;
}

__attribute__((constructor)) static void nvfix_constructor(void) {
    const char *explicit_uuid;

    nvfix_config_load(&g_nvfix_config);
    explicit_uuid = g_nvfix_config.gpu_uuid_set ? g_nvfix_config.gpu_uuid : NULL;

    if (g_nvfix_config.proc_filter_enabled) {
        g_proc_filter_ready = nvfix_proc_filter_runtime_init(explicit_uuid, &g_allowed_gpus);
        if (!g_proc_filter_ready) {
            nvfix_log_warn(&g_nvfix_config, "proc filter enabled but GPU topology detection failed");
        }
    }

    nvfix_log_trace(&g_nvfix_config,
                    "loaded trace=%d proc=%d ioctl=%d",
                    g_nvfix_config.trace_enabled,
                    g_nvfix_config.proc_filter_enabled,
                    g_nvfix_config.ioctl_filter_enabled);
}
