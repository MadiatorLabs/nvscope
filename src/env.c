#include "env.h"

#include <stdlib.h>
#include <string.h>

int nvfix_env_truthy(const char *value) {
    if (value == NULL) {
        return 0;
    }

    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "TRUE") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "YES") == 0 ||
           strcmp(value, "on") == 0 ||
           strcmp(value, "ON") == 0;
}

static const char *nvfix_getenv_alias(const char *primary, const char *legacy) {
    const char *value;

    value = getenv(primary);
    if (value != NULL) {
        return value;
    }

    return getenv(legacy);
}

void nvfix_config_load(struct nvfix_config *cfg) {
    const char *proc_env;
    const char *gpu_uuid;

    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    cfg->trace_enabled =
        nvfix_env_truthy(nvfix_getenv_alias("NVSCOPE_TRACE", "NV_VIDEO_FIX_TRACE"));
    cfg->ioctl_filter_enabled =
        nvfix_env_truthy(nvfix_getenv_alias("NVSCOPE_IOCTL", "NV_VIDEO_FIX_IOCTL"));

    proc_env = nvfix_getenv_alias("NVSCOPE_PROC", "NV_VIDEO_FIX_PROC");
    cfg->proc_filter_enabled = proc_env == NULL ? 1 : nvfix_env_truthy(proc_env);

    gpu_uuid = nvfix_getenv_alias("NVSCOPE_GPU_UUID", "NV_VIDEO_FIX_GPU_UUID");
    if (gpu_uuid != NULL && gpu_uuid[0] != '\0') {
        cfg->gpu_uuid_set = 1;
        strncpy(cfg->gpu_uuid, gpu_uuid, sizeof(cfg->gpu_uuid) - 1);
        cfg->gpu_uuid[sizeof(cfg->gpu_uuid) - 1] = '\0';
    }
}
