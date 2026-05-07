#include "proc_filter.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static int nvfix_sys_openat(int dirfd, const char *path, int flags) {
    return (int)syscall(SYS_openat, dirfd, path, flags, 0);
}

static int nvfix_sys_close(int fd) {
    int saved_errno;
    int result;

    saved_errno = errno;
    result = (int)syscall(SYS_close, fd);
    errno = saved_errno;
    return result;
}

static int nvfix_name_is_nvidia_minor(const char *name, int *minor) {
    const char prefix[] = "nvidia";
    const char *cursor;
    int value;

    if (name == NULL || strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }

    cursor = name + sizeof(prefix) - 1;
    if (*cursor < '0' || *cursor > '9') {
        return 0;
    }

    value = 0;
    while (*cursor != '\0') {
        int digit;

        if (*cursor < '0' || *cursor > '9') {
            return 0;
        }

        digit = *cursor - '0';
        if (value > (INT_MAX - digit) / 10) {
            return 0;
        }

        value = (value * 10) + digit;
        cursor++;
    }

    *minor = value;
    return 1;
}

static void nvfix_zero_gpu_info(struct nvfix_gpu_info *info) {
    if (info == NULL) {
        return;
    }

    memset(info, 0, sizeof(*info));
    info->device_minor = -1;
}

static void nvfix_trim_slice(const char **start, size_t *len) {
    const char *cursor;
    size_t remaining;

    cursor = *start;
    remaining = *len;

    while (remaining > 0 &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')) {
        cursor++;
        remaining--;
    }

    while (remaining > 0) {
        char c = cursor[remaining - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        remaining--;
    }

    *start = cursor;
    *len = remaining;
}

static int nvfix_copy_slice_exact(char *dst, size_t dst_size, const char *src, size_t len) {
    if (dst == NULL || dst_size == 0 || src == NULL || len >= dst_size) {
        return 0;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

static int nvfix_parse_nonnegative_int(const char *src, size_t len, int *value) {
    size_t i;
    int parsed;

    if (src == NULL || value == NULL || len == 0) {
        return 0;
    }

    parsed = 0;
    for (i = 0; i < len; i++) {
        int digit;

        if (src[i] < '0' || src[i] > '9') {
            return 0;
        }

        digit = src[i] - '0';
        if (parsed > (INT_MAX - digit) / 10) {
            return 0;
        }

        parsed = (parsed * 10) + digit;
    }

    *value = parsed;
    return 1;
}

static int nvfix_value_after_key(const char *line,
                                 size_t line_len,
                                 const char *key,
                                 const char **value,
                                 size_t *value_len) {
    size_t key_len;
    const char *start;
    size_t len;

    key_len = strlen(key);
    if (line_len < key_len || memcmp(line, key, key_len) != 0) {
        return 0;
    }

    start = line + key_len;
    len = line_len - key_len;
    nvfix_trim_slice(&start, &len);

    *value = start;
    *value_len = len;
    return 1;
}

static int nvfix_extract_proc_bus(const char *path, char *bus_id, size_t bus_id_size) {
    const char prefix[] = "/proc/driver/nvidia/gpus/";
    const char suffix[] = "/information";
    const char *bus_start;
    const char *suffix_start;
    size_t path_len;
    size_t prefix_len;
    size_t suffix_len;
    size_t bus_len;
    size_t i;

    if (path == NULL) {
        return 0;
    }

    prefix_len = sizeof(prefix) - 1;
    suffix_len = sizeof(suffix) - 1;
    path_len = strlen(path);

    if (path_len <= prefix_len + suffix_len ||
        strncmp(path, prefix, prefix_len) != 0 ||
        strcmp(path + path_len - suffix_len, suffix) != 0) {
        return 0;
    }

    bus_start = path + prefix_len;
    suffix_start = path + path_len - suffix_len;
    bus_len = (size_t)(suffix_start - bus_start);
    if (bus_len == 0 || bus_len >= bus_id_size) {
        return 0;
    }

    for (i = 0; i < bus_len; i++) {
        if (bus_start[i] == '/') {
            return 0;
        }
    }

    return nvfix_copy_slice_exact(bus_id, bus_id_size, bus_start, bus_len);
}

int nvfix_parse_gpu_information(const char *path, const char *text, struct nvfix_gpu_info *info) {
    size_t offset;
    size_t len;
    int ok;

    if (info == NULL) {
        return 0;
    }

    nvfix_zero_gpu_info(info);
    if (text == NULL) {
        return 0;
    }

    ok = 1;
    len = strlen(text);
    offset = 0;
    while (offset < len) {
        const char *line;
        const char *value;
        size_t line_len;
        size_t value_len;
        size_t next;

        next = offset;
        while (next < len && text[next] != '\n') {
            next++;
        }

        line = text + offset;
        line_len = next - offset;
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
        }

        if (nvfix_value_after_key(line, line_len, "GPU UUID:", &value, &value_len)) {
            if (!nvfix_copy_slice_exact(info->uuid, sizeof(info->uuid), value, value_len)) {
                ok = 0;
            }
        } else if (nvfix_value_after_key(line, line_len, "Bus Location:", &value, &value_len)) {
            if (value_len == 0 ||
                !nvfix_copy_slice_exact(info->bus_id, sizeof(info->bus_id), value, value_len)) {
                ok = 0;
            }
        } else if (nvfix_value_after_key(line, line_len, "Device Minor:", &value, &value_len)) {
            if (!nvfix_parse_nonnegative_int(value, value_len, &info->device_minor)) {
                ok = 0;
            }
        }

        offset = next;
        if (offset < len && text[offset] == '\n') {
            offset++;
        }
    }

    if (ok && info->bus_id[0] == '\0') {
        if (!nvfix_extract_proc_bus(path, info->bus_id, sizeof(info->bus_id))) {
            ok = 0;
        }
    }

    info->present = (ok && info->device_minor >= 0 && info->bus_id[0] != '\0') ? 1 : 0;
    return ok && info->present;
}

static int nvfix_minor_is_mounted(int minor, const int *mounted_minors, size_t mounted_minor_count) {
    size_t i;

    if (mounted_minors == NULL) {
        return 0;
    }

    for (i = 0; i < mounted_minor_count; i++) {
        if (mounted_minors[i] == minor) {
            return 1;
        }
    }

    return 0;
}

int nvfix_build_allowed_gpus(const struct nvfix_gpu_info *infos,
                             size_t info_count,
                             const int *mounted_minors,
                             size_t mounted_minor_count,
                             const char *explicit_uuid,
                             struct nvfix_allowed_gpus *allowed) {
    size_t i;

    if (allowed == NULL) {
        return 0;
    }

    memset(allowed, 0, sizeof(*allowed));

    if (infos == NULL || info_count == 0 || mounted_minors == NULL || mounted_minor_count == 0) {
        return 0;
    }

    for (i = 0; i < info_count && allowed->count < NVFIX_MAX_GPUS; i++) {
        if (!infos[i].present) {
            continue;
        }

        if (!nvfix_minor_is_mounted(infos[i].device_minor, mounted_minors, mounted_minor_count)) {
            continue;
        }

        if (explicit_uuid != NULL && explicit_uuid[0] != '\0' &&
            strcmp(infos[i].uuid, explicit_uuid) != 0) {
            continue;
        }

        allowed->gpus[allowed->count] = infos[i];
        allowed->count++;
    }

    allowed->valid = allowed->count > 0 ? 1 : 0;
    return allowed->valid;
}

static size_t nvfix_collect_mounted_minors(int *mounted_minors, size_t mounted_capacity) {
    char buffer[4096];
    int fd;
    size_t count;

    if (mounted_minors == NULL || mounted_capacity == 0) {
        return 0;
    }

    fd = nvfix_sys_openat(AT_FDCWD, "/dev", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }

    count = 0;
    for (;;) {
        long nread;
        long offset;

        nread = syscall(SYS_getdents64, fd, buffer, sizeof(buffer));
        if (nread <= 0) {
            break;
        }

        offset = 0;
        while (offset < nread) {
            const struct linux_dirent64 *entry =
                (const struct linux_dirent64 *)(const void *)(buffer + offset);
            int minor;

            if (entry->d_reclen == 0 || offset + entry->d_reclen > nread) {
                nvfix_sys_close(fd);
                return count;
            }

            if (count < mounted_capacity && nvfix_name_is_nvidia_minor(entry->d_name, &minor)) {
                mounted_minors[count] = minor;
                count++;
            }

            offset += entry->d_reclen;
        }
    }

    nvfix_sys_close(fd);
    return count;
}

static int nvfix_read_file_to_buffer(const char *path, char *buffer, size_t buffer_size) {
    int fd;
    size_t used;

    if (path == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }

    fd = nvfix_sys_openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }

    used = 0;
    while (used + 1 < buffer_size) {
        long nread;

        nread = syscall(SYS_read, fd, buffer + used, buffer_size - used - 1);
        if (nread < 0) {
            nvfix_sys_close(fd);
            return 0;
        }
        if (nread == 0) {
            break;
        }

        used += (size_t)nread;
    }

    buffer[used] = '\0';
    nvfix_sys_close(fd);
    return used > 0 ? 1 : 0;
}

static size_t nvfix_collect_gpu_infos(struct nvfix_gpu_info *infos, size_t info_capacity) {
    char dir_buffer[4096];
    char text_buffer[8192];
    int fd;
    size_t count;

    if (infos == NULL || info_capacity == 0) {
        return 0;
    }

    fd = nvfix_sys_openat(AT_FDCWD,
                          "/proc/driver/nvidia/gpus",
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }

    count = 0;
    for (;;) {
        long nread;
        long offset;

        nread = syscall(SYS_getdents64, fd, dir_buffer, sizeof(dir_buffer));
        if (nread <= 0) {
            break;
        }

        offset = 0;
        while (offset < nread) {
            const struct linux_dirent64 *entry =
                (const struct linux_dirent64 *)(const void *)(dir_buffer + offset);
            char path[PATH_MAX];

            if (entry->d_reclen == 0 || offset + entry->d_reclen > nread) {
                nvfix_sys_close(fd);
                return count;
            }

            if (strcmp(entry->d_name, ".") != 0 &&
                strcmp(entry->d_name, "..") != 0 &&
                count < info_capacity) {
                int written;

                written = snprintf(path,
                                   sizeof(path),
                                   "/proc/driver/nvidia/gpus/%s/information",
                                   entry->d_name);
                if (written > 0 && (size_t)written < sizeof(path) &&
                    nvfix_read_file_to_buffer(path, text_buffer, sizeof(text_buffer)) &&
                    nvfix_parse_gpu_information(path, text_buffer, &infos[count])) {
                    count++;
                }
            }

            offset += entry->d_reclen;
        }
    }

    nvfix_sys_close(fd);
    return count;
}

int nvfix_proc_filter_runtime_init(const char *explicit_uuid, struct nvfix_allowed_gpus *out) {
    struct nvfix_gpu_info infos[NVFIX_MAX_GPUS];
    int mounted_minors[NVFIX_MAX_GPUS];
    size_t info_count;
    size_t mounted_count;

    if (out == NULL) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    mounted_count = nvfix_collect_mounted_minors(mounted_minors, NVFIX_MAX_GPUS);
    info_count = nvfix_collect_gpu_infos(infos, NVFIX_MAX_GPUS);

    return nvfix_build_allowed_gpus(infos,
                                    info_count,
                                    mounted_minors,
                                    mounted_count,
                                    explicit_uuid,
                                    out);
}

int nvfix_allowed_contains_bus(const struct nvfix_allowed_gpus *allowed, const char *bus_id) {
    size_t i;

    if (allowed == NULL || !allowed->valid || bus_id == NULL || bus_id[0] == '\0') {
        return 0;
    }

    for (i = 0; i < allowed->count; i++) {
        if (strcmp(allowed->gpus[i].bus_id, bus_id) == 0) {
            return 1;
        }
    }

    return 0;
}

enum nvfix_proc_decision nvfix_proc_path_allowed(const struct nvfix_allowed_gpus *allowed,
                                                 const char *path) {
    char bus_id[NVFIX_PROC_BUS_ID_MAX];

    if (path == NULL) {
        return NVFIX_PROC_IGNORE;
    }

    if (strcmp(path, "/proc/driver/nvidia/gpus") == 0) {
        return NVFIX_PROC_ALLOW;
    }

    if (!nvfix_extract_proc_bus(path, bus_id, sizeof(bus_id))) {
        return NVFIX_PROC_IGNORE;
    }

    if (allowed == NULL || !allowed->valid) {
        return NVFIX_PROC_ALLOW;
    }

    return nvfix_allowed_contains_bus(allowed, bus_id) ? NVFIX_PROC_ALLOW : NVFIX_PROC_DENY;
}

static int nvfix_dirent_name_is_valid(const struct linux_dirent64 *entry,
                                      ssize_t remaining,
                                      size_t header_size) {
    size_t name_capacity;

    if (entry->d_reclen < header_size || (ssize_t)entry->d_reclen > remaining) {
        return 0;
    }

    name_capacity = entry->d_reclen - header_size;
    return memchr(entry->d_name, '\0', name_capacity) != NULL;
}

ssize_t nvfix_filter_dirent64_buffer(char *buffer,
                                     ssize_t len,
                                     const struct nvfix_allowed_gpus *allowed) {
    ssize_t read_offset;
    ssize_t write_offset;
    const size_t header_size = offsetof(struct linux_dirent64, d_name);

    if (buffer == NULL || len <= 0 || allowed == NULL || !allowed->valid) {
        return len;
    }

    read_offset = 0;
    write_offset = 0;
    while (read_offset < len) {
        struct linux_dirent64 *entry;
        int keep;

        if (len - read_offset < (ssize_t)header_size) {
            return len;
        }

        entry = (struct linux_dirent64 *)(void *)(buffer + read_offset);
        if (!nvfix_dirent_name_is_valid(entry, len - read_offset, header_size)) {
            return len;
        }

        keep = (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0 ||
                nvfix_allowed_contains_bus(allowed, entry->d_name));

        if (keep) {
            if (write_offset != read_offset) {
                memmove(buffer + write_offset, buffer + read_offset, entry->d_reclen);
            }
            write_offset += entry->d_reclen;
        }

        read_offset += entry->d_reclen;
    }

    return write_offset;
}
