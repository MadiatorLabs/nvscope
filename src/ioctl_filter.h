#ifndef NVFIX_IOCTL_FILTER_H
#define NVFIX_IOCTL_FILTER_H

#include "env.h"
#include "proc_filter.h"

void nvfix_ioctl_filter_after(const struct nvfix_config *cfg,
                              const struct nvfix_allowed_gpus *allowed,
                              unsigned long request,
                              void *arg,
                              int result);

#ifdef NVFIX_TESTING
void nvfix_test_ioctl_filter_reset(void);
int nvfix_test_ioctl_allowed_gpu_id_count(void);
#endif

#endif
