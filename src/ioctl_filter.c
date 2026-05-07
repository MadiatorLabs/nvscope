#include "ioctl_filter.h"

#include "log.h"
#include "nvidia_rm.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

static pthread_mutex_t g_ioctl_filter_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_allowed_gpu_ids[NVFIX_MAX_GPUS];
static size_t g_allowed_gpu_id_count;

static int nvfix_allowed_contains_minor(const struct nvfix_allowed_gpus *allowed, uint32_t minor) {
    size_t i;

    if (allowed == NULL || !allowed->valid) {
        return 0;
    }

    for (i = 0; i < allowed->count; i++) {
        if (allowed->gpus[i].present &&
            allowed->gpus[i].device_minor >= 0 &&
            (uint32_t)allowed->gpus[i].device_minor == minor) {
            return 1;
        }
    }

    return 0;
}

static int nvfix_gpu_id_is_allowed_locked(uint32_t gpu_id) {
    size_t i;

    for (i = 0; i < g_allowed_gpu_id_count; i++) {
        if (g_allowed_gpu_ids[i] == gpu_id) {
            return 1;
        }
    }

    return 0;
}

static void nvfix_remember_allowed_gpu_id_locked(uint32_t gpu_id) {
    if (gpu_id == NVFIX_NV0000_CTRL_GPU_INVALID_ID ||
        nvfix_gpu_id_is_allowed_locked(gpu_id) ||
        g_allowed_gpu_id_count >= NVFIX_MAX_GPUS) {
        return;
    }

    g_allowed_gpu_ids[g_allowed_gpu_id_count] = gpu_id;
    g_allowed_gpu_id_count++;
}

static void nvfix_record_card_info_gpu_ids(const struct nvfix_allowed_gpus *allowed,
                                           const struct nvfix_nv_ioctl_card_info *cards,
                                           size_t count) {
    size_t i;

    if (allowed == NULL || !allowed->valid || cards == NULL) {
        return;
    }

    pthread_mutex_lock(&g_ioctl_filter_lock);
    for (i = 0; i < count; i++) {
        if (cards[i].valid && nvfix_allowed_contains_minor(allowed, cards[i].minor_number)) {
            nvfix_remember_allowed_gpu_id_locked(cards[i].gpu_id);
        }
    }
    pthread_mutex_unlock(&g_ioctl_filter_lock);
}

static void nvfix_filter_card_info(const struct nvfix_allowed_gpus *allowed,
                                   struct nvfix_nv_ioctl_card_info *cards,
                                   size_t count) {
    size_t i;

    if (allowed == NULL || !allowed->valid || cards == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (cards[i].valid && !nvfix_allowed_contains_minor(allowed, cards[i].minor_number)) {
            cards[i].valid = 0;
        }
    }
}

static int nvfix_gpu_id_array_filter_enabled(const struct nvfix_config *cfg,
                                             const struct nvfix_allowed_gpus *allowed,
                                             int result,
                                             const struct nvfix_nvos54_parameters *rm) {
    return cfg != NULL &&
           cfg->ioctl_filter_enabled &&
           allowed != NULL &&
           allowed->valid &&
           result == 0 &&
           rm != NULL &&
           rm->status == 0 &&
           rm->params != 0;
}

static size_t nvfix_filter_gpu_id_array(uint32_t *gpu_ids, size_t count) {
    uint32_t kept[NVFIX_MAX_GPUS];
    size_t kept_count;
    size_t i;

    if (gpu_ids == NULL || count == 0) {
        return 0;
    }

    pthread_mutex_lock(&g_ioctl_filter_lock);
    if (g_allowed_gpu_id_count == 0) {
        pthread_mutex_unlock(&g_ioctl_filter_lock);
        return 0;
    }

    kept_count = 0;
    for (i = 0; i < count; i++) {
        uint32_t gpu_id = gpu_ids[i];

        if (gpu_id == NVFIX_NV0000_CTRL_GPU_INVALID_ID) {
            break;
        }

        if (nvfix_gpu_id_is_allowed_locked(gpu_id) && kept_count < NVFIX_MAX_GPUS) {
            kept[kept_count] = gpu_id;
            kept_count++;
        }
    }
    pthread_mutex_unlock(&g_ioctl_filter_lock);

    if (kept_count == 0) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        gpu_ids[i] = i < kept_count ? kept[i] : NVFIX_NV0000_CTRL_GPU_INVALID_ID;
    }

    return kept_count;
}

static void nvfix_filter_active_devices(struct nvfix_nv0000_ctrl_gpu_get_active_device_ids_params *params) {
    struct nvfix_nv0000_ctrl_gpu_active_device kept[NVFIX_NV0000_CTRL_GPU_MAX_ACTIVE_DEVICES];
    uint32_t kept_count;
    uint32_t i;
    uint32_t count;

    if (params == NULL) {
        return;
    }

    pthread_mutex_lock(&g_ioctl_filter_lock);
    if (g_allowed_gpu_id_count == 0) {
        pthread_mutex_unlock(&g_ioctl_filter_lock);
        return;
    }

    count = params->numDevices;
    if (count > NVFIX_NV0000_CTRL_GPU_MAX_ACTIVE_DEVICES) {
        count = NVFIX_NV0000_CTRL_GPU_MAX_ACTIVE_DEVICES;
    }

    kept_count = 0;
    for (i = 0; i < count; i++) {
        if (nvfix_gpu_id_is_allowed_locked(params->devices[i].gpuId)) {
            kept[kept_count] = params->devices[i];
            kept_count++;
        }
    }
    pthread_mutex_unlock(&g_ioctl_filter_lock);

    if (kept_count == 0) {
        return;
    }

    if (kept_count > 0) {
        memcpy(params->devices, kept, sizeof(kept[0]) * kept_count);
    }
    params->numDevices = kept_count;
}

static void nvfix_filter_rm_control(const struct nvfix_config *cfg,
                                    const struct nvfix_allowed_gpus *allowed,
                                    struct nvfix_nvos54_parameters *rm,
                                    int result) {
    if (!nvfix_gpu_id_array_filter_enabled(cfg, allowed, result, rm)) {
        return;
    }

    if (rm->cmd == NVFIX_NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS &&
        rm->paramsSize >= sizeof(uint32_t) * NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS) {
        uint32_t *gpu_ids = (uint32_t *)(uintptr_t)rm->params;
        size_t kept = nvfix_filter_gpu_id_array(gpu_ids,
                                                NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS);
        if (kept > 0) {
            nvfix_log_trace(cfg, "filtered RM attached gpuIds to %zu allowed entries", kept);
        }
    } else if (rm->cmd == NVFIX_NV0000_CTRL_CMD_GPU_GET_ACTIVE_DEVICE_IDS &&
               rm->paramsSize >=
                   sizeof(struct nvfix_nv0000_ctrl_gpu_get_active_device_ids_params)) {
        nvfix_filter_active_devices(
            (struct nvfix_nv0000_ctrl_gpu_get_active_device_ids_params *)(uintptr_t)rm->params);
        nvfix_log_trace(cfg, "filtered RM active device ids to allowed gpuIds");
    }
}

void nvfix_ioctl_filter_after(const struct nvfix_config *cfg,
                              const struct nvfix_allowed_gpus *allowed,
                              unsigned long request,
                              void *arg,
                              int result) {
    if (result != 0 || arg == NULL) {
        return;
    }

    if (nvfix_request_is_card_info(request)) {
        struct nvfix_nv_ioctl_card_info *cards = (struct nvfix_nv_ioctl_card_info *)arg;

        nvfix_record_card_info_gpu_ids(allowed, cards, NVFIX_NV_MAX_DEVICES);
        if (cfg != NULL && cfg->ioctl_filter_enabled) {
            nvfix_filter_card_info(allowed, cards, NVFIX_NV_MAX_DEVICES);
        }
        return;
    }

    if (nvfix_request_is_rm_control(request)) {
        nvfix_filter_rm_control(cfg,
                                allowed,
                                (struct nvfix_nvos54_parameters *)arg,
                                result);
    }
}

#ifdef NVFIX_TESTING
void nvfix_test_ioctl_filter_reset(void) {
    pthread_mutex_lock(&g_ioctl_filter_lock);
    memset(g_allowed_gpu_ids, 0, sizeof(g_allowed_gpu_ids));
    g_allowed_gpu_id_count = 0;
    pthread_mutex_unlock(&g_ioctl_filter_lock);
}

int nvfix_test_ioctl_allowed_gpu_id_count(void) {
    int result;

    pthread_mutex_lock(&g_ioctl_filter_lock);
    result = (int)g_allowed_gpu_id_count;
    pthread_mutex_unlock(&g_ioctl_filter_lock);
    return result;
}
#endif
