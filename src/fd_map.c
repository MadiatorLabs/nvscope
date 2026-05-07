#include "fd_map.h"

#include <limits.h>
#include <pthread.h>
#include <string.h>

/* Fixed table required by the plan: only file descriptors 0 through 4095 are tracked. */
#define NVFIX_FD_MAP_MAX 4096

struct nvfix_fd_slot {
    int occupied;
    struct nvfix_fd_record record;
};

static pthread_mutex_t g_fd_map_lock = PTHREAD_MUTEX_INITIALIZER;
static struct nvfix_fd_slot g_fd_map[NVFIX_FD_MAP_MAX];

static void nvfix_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void nvfix_copy_slice(char *dst, size_t dst_size, const char *src, size_t len) {
    size_t copy_len;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    copy_len = len;
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static int nvfix_parse_nvidia_minor(const char *path, int *minor) {
    const char prefix[] = "/dev/nvidia";
    const char *cursor;
    int value;
    int digit;

    if (strncmp(path, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }

    cursor = path + sizeof(prefix) - 1;
    if (*cursor < '0' || *cursor > '9') {
        return 0;
    }

    value = 0;
    while (*cursor != '\0') {
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

static int nvfix_classify_proc_gpu_path(const char *path, struct nvfix_fd_record *record) {
    const char prefix[] = "/proc/driver/nvidia/gpus/";
    const char suffix[] = "/information";
    const char *bus_start;
    const char *suffix_start;
    size_t path_len;
    size_t prefix_len;
    size_t suffix_len;
    size_t bus_len;
    size_t i;

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
    if (bus_len == 0 || bus_len >= sizeof(record->bus_id)) {
        return 0;
    }

    for (i = 0; i < bus_len; i++) {
        if (bus_start[i] == '/') {
            return 0;
        }
    }

    record->kind = NVFIX_FD_PROC_GPU_INFO;
    nvfix_copy_slice(record->bus_id, sizeof(record->bus_id), bus_start, bus_len);
    return 1;
}

int nvfix_classify_path(const char *path, struct nvfix_fd_record *record) {
    int minor;

    if (record == NULL) {
        return 0;
    }

    memset(record, 0, sizeof(*record));
    record->kind = NVFIX_FD_OTHER;
    record->device_minor = -1;
    nvfix_copy_string(record->path, sizeof(record->path), path);

    if (path == NULL) {
        return 0;
    }

    if (strcmp(path, "/dev/nvidiactl") == 0) {
        record->kind = NVFIX_FD_NVIDIA_CTL;
        return 1;
    }

    if (strcmp(path, "/dev/nvidia-uvm") == 0) {
        record->kind = NVFIX_FD_NVIDIA_UVM;
        return 1;
    }

    if (nvfix_parse_nvidia_minor(path, &minor)) {
        record->kind = NVFIX_FD_NVIDIA_DEVICE;
        record->device_minor = minor;
        return 1;
    }

    if (strcmp(path, "/proc/driver/nvidia/gpus") == 0) {
        record->kind = NVFIX_FD_PROC_GPUS_DIR;
        return 1;
    }

    return nvfix_classify_proc_gpu_path(path, record);
}

void nvfix_fd_map_clear(void) {
    pthread_mutex_lock(&g_fd_map_lock);
    memset(g_fd_map, 0, sizeof(g_fd_map));
    pthread_mutex_unlock(&g_fd_map_lock);
}

void nvfix_fd_map_set(int fd, const struct nvfix_fd_record *record) {
    if (fd < 0 || fd >= NVFIX_FD_MAP_MAX || record == NULL) {
        return;
    }

    pthread_mutex_lock(&g_fd_map_lock);
    g_fd_map[fd].occupied = 1;
    g_fd_map[fd].record = *record;
    pthread_mutex_unlock(&g_fd_map_lock);
}

int nvfix_fd_map_get(int fd, struct nvfix_fd_record *record) {
    int found;

    if (fd < 0 || fd >= NVFIX_FD_MAP_MAX || record == NULL) {
        return 0;
    }

    pthread_mutex_lock(&g_fd_map_lock);
    found = g_fd_map[fd].occupied;
    if (found) {
        *record = g_fd_map[fd].record;
    }
    pthread_mutex_unlock(&g_fd_map_lock);

    return found;
}

void nvfix_fd_map_remove(int fd) {
    if (fd < 0 || fd >= NVFIX_FD_MAP_MAX) {
        return;
    }

    pthread_mutex_lock(&g_fd_map_lock);
    memset(&g_fd_map[fd], 0, sizeof(g_fd_map[fd]));
    pthread_mutex_unlock(&g_fd_map_lock);
}
