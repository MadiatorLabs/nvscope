#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static void nvfix_vlog(const struct nvfix_config *cfg,
                       const char *prefix,
                       const char *fmt,
                       va_list args) {
    if (cfg == NULL || !cfg->trace_enabled) {
        return;
    }

    fputs(prefix, stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void nvfix_log_trace(const struct nvfix_config *cfg, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    nvfix_vlog(cfg, "[nvscope] ", fmt, args);
    va_end(args);
}

void nvfix_log_warn(const struct nvfix_config *cfg, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    nvfix_vlog(cfg, "[nvscope] warning: ", fmt, args);
    va_end(args);
}
