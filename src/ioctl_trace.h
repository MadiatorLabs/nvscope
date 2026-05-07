#ifndef NVFIX_IOCTL_TRACE_H
#define NVFIX_IOCTL_TRACE_H

#include "env.h"

void nvfix_ioctl_trace(const struct nvfix_config *cfg,
                       int fd,
                       unsigned long request,
                       void *arg,
                       int result);

#endif
