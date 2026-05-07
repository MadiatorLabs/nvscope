#include "ioctl_trace.h"

#include "log.h"
#include "nvidia_rm.h"

#include <stdint.h>

void nvfix_ioctl_trace(const struct nvfix_config *cfg,
                       int fd,
                       unsigned long request,
                       void *arg,
                       int result) {
    if (nvfix_request_is_rm_control(request) && arg != NULL) {
        const struct nvfix_nvos54_parameters *rm =
            (const struct nvfix_nvos54_parameters *)arg;

        nvfix_log_trace(cfg,
                        "ioctl fd %d request 0x%lx esc=RM_CONTROL cmd=0x%x(%s) params=%p paramsSize=%u status=0x%x result=%d",
                        fd,
                        request,
                        rm->cmd,
                        nvfix_rm_control_cmd_name(rm->cmd),
                        (void *)(uintptr_t)rm->params,
                        rm->paramsSize,
                        (unsigned int)rm->status,
                        result);
        return;
    }

    if (nvfix_request_is_card_info(request)) {
        nvfix_log_trace(cfg,
                        "ioctl fd %d request 0x%lx esc=CARD_INFO entries=%u arg %p result %d",
                        fd,
                        request,
                        NVFIX_NV_MAX_DEVICES,
                        arg,
                        result);
        return;
    }

    nvfix_log_trace(cfg,
                    "ioctl fd %d request 0x%lx arg %p result %d",
                    fd,
                    request,
                    arg,
                    result);
}
