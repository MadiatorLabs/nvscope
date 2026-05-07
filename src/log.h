#ifndef NVFIX_LOG_H
#define NVFIX_LOG_H

#include "env.h"

void nvfix_log_trace(const struct nvfix_config *cfg, const char *fmt, ...);
void nvfix_log_warn(const struct nvfix_config *cfg, const char *fmt, ...);

#endif
