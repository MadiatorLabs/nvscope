#ifndef NVFIX_PROC_FILTER_H
#define NVFIX_PROC_FILTER_H

#include <stddef.h>
#include <sys/types.h>

#define NVFIX_MAX_GPUS 32
#define NVFIX_PROC_BUS_ID_MAX 32
#define NVFIX_PROC_UUID_MAX 96

struct nvfix_gpu_info {
    int present;
    int device_minor;
    char bus_id[NVFIX_PROC_BUS_ID_MAX];
    char uuid[NVFIX_PROC_UUID_MAX];
};

struct nvfix_allowed_gpus {
    int valid;
    size_t count;
    struct nvfix_gpu_info gpus[NVFIX_MAX_GPUS];
};

enum nvfix_proc_decision {
    NVFIX_PROC_IGNORE = 0,
    NVFIX_PROC_ALLOW,
    NVFIX_PROC_DENY
};

int nvfix_parse_gpu_information(const char *path, const char *text, struct nvfix_gpu_info *out);
int nvfix_build_allowed_gpus(const struct nvfix_gpu_info *infos,
                             size_t info_count,
                             const int *mounted_minors,
                             size_t mounted_count,
                             const char *explicit_uuid,
                             struct nvfix_allowed_gpus *out);
int nvfix_proc_filter_runtime_init(const char *explicit_uuid, struct nvfix_allowed_gpus *out);
int nvfix_allowed_contains_bus(const struct nvfix_allowed_gpus *allowed, const char *bus_id);
enum nvfix_proc_decision nvfix_proc_path_allowed(const struct nvfix_allowed_gpus *allowed,
                                                 const char *path);
ssize_t nvfix_filter_dirent64_buffer(char *buffer,
                                     ssize_t len,
                                     const struct nvfix_allowed_gpus *allowed);

#endif
