#ifndef NVFIX_ENV_H
#define NVFIX_ENV_H

#define NVFIX_GPU_UUID_MAX 96

struct nvfix_config {
    int trace_enabled;
    int proc_filter_enabled;
    int ioctl_filter_enabled;
    int gpu_uuid_set;
    char gpu_uuid[NVFIX_GPU_UUID_MAX];
};

int nvfix_env_truthy(const char *value);
void nvfix_config_load(struct nvfix_config *cfg);

#endif
