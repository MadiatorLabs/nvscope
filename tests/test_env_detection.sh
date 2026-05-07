#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p tmp
cat >tmp/test_env_detection.c <<'EOF'
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", name, got, want);
        failures++;
    }
}

static void expect_string(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", name, got, want);
        failures++;
    }
}

static void test_truthy_values(void) {
    const char *truthy[] = {"1", "true", "TRUE", "yes", "YES", "on", "ON"};
    size_t i;

    for (i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i++) {
        expect_int(truthy[i], nvfix_env_truthy(truthy[i]), 1);
    }

    expect_int("NULL", nvfix_env_truthy(NULL), 0);
    expect_int("empty", nvfix_env_truthy(""), 0);
    expect_int("0", nvfix_env_truthy("0"), 0);
    expect_int("false", nvfix_env_truthy("false"), 0);
    expect_int("True", nvfix_env_truthy("True"), 0);
}

static void clear_env(void) {
    unsetenv("NVSCOPE_TRACE");
    unsetenv("NVSCOPE_PROC");
    unsetenv("NVSCOPE_IOCTL");
    unsetenv("NVSCOPE_GPU_UUID");
    unsetenv("NV_VIDEO_FIX_TRACE");
    unsetenv("NV_VIDEO_FIX_PROC");
    unsetenv("NV_VIDEO_FIX_IOCTL");
    unsetenv("NV_VIDEO_FIX_GPU_UUID");
}

static void test_config_defaults(void) {
    struct nvfix_config cfg;

    clear_env();
    nvfix_config_load(&cfg);

    expect_int("trace default", cfg.trace_enabled, 0);
    expect_int("proc default", cfg.proc_filter_enabled, 1);
    expect_int("ioctl default", cfg.ioctl_filter_enabled, 0);
    expect_int("gpu uuid default", cfg.gpu_uuid_set, 0);
    expect_string("gpu uuid default string", cfg.gpu_uuid, "");
}

static void test_config_env_values(void) {
    struct nvfix_config cfg;

    clear_env();
    setenv("NVSCOPE_TRACE", "yes", 1);
    setenv("NVSCOPE_PROC", "0", 1);
    setenv("NVSCOPE_IOCTL", "ON", 1);
    setenv("NVSCOPE_GPU_UUID", "GPU-1234", 1);
    nvfix_config_load(&cfg);

    expect_int("trace yes", cfg.trace_enabled, 1);
    expect_int("proc disabled", cfg.proc_filter_enabled, 0);
    expect_int("ioctl on", cfg.ioctl_filter_enabled, 1);
    expect_int("gpu uuid set", cfg.gpu_uuid_set, 1);
    expect_string("gpu uuid copied", cfg.gpu_uuid, "GPU-1234");
}

static void test_legacy_env_values_still_work(void) {
    struct nvfix_config cfg;

    clear_env();
    setenv("NV_VIDEO_FIX_TRACE", "yes", 1);
    setenv("NV_VIDEO_FIX_PROC", "0", 1);
    setenv("NV_VIDEO_FIX_IOCTL", "ON", 1);
    setenv("NV_VIDEO_FIX_GPU_UUID", "GPU-legacy", 1);
    nvfix_config_load(&cfg);

    expect_int("legacy trace yes", cfg.trace_enabled, 1);
    expect_int("legacy proc disabled", cfg.proc_filter_enabled, 0);
    expect_int("legacy ioctl on", cfg.ioctl_filter_enabled, 1);
    expect_int("legacy gpu uuid set", cfg.gpu_uuid_set, 1);
    expect_string("legacy gpu uuid copied", cfg.gpu_uuid, "GPU-legacy");
}

static void test_nvscope_env_takes_precedence(void) {
    struct nvfix_config cfg;

    clear_env();
    setenv("NVSCOPE_TRACE", "0", 1);
    setenv("NVSCOPE_IOCTL", "1", 1);
    setenv("NVSCOPE_GPU_UUID", "GPU-new", 1);
    setenv("NV_VIDEO_FIX_TRACE", "1", 1);
    setenv("NV_VIDEO_FIX_IOCTL", "0", 1);
    setenv("NV_VIDEO_FIX_GPU_UUID", "GPU-old", 1);
    nvfix_config_load(&cfg);

    expect_int("new trace wins", cfg.trace_enabled, 0);
    expect_int("new ioctl wins", cfg.ioctl_filter_enabled, 1);
    expect_string("new gpu uuid wins", cfg.gpu_uuid, "GPU-new");
}

static void test_gpu_uuid_truncates_safely(void) {
    struct nvfix_config cfg;
    char uuid[NVFIX_GPU_UUID_MAX * 2];
    size_t i;

    clear_env();
    for (i = 0; i < sizeof(uuid) - 1; i++) {
        uuid[i] = 'A';
    }
    uuid[sizeof(uuid) - 1] = '\0';

    setenv("NVSCOPE_GPU_UUID", uuid, 1);
    nvfix_config_load(&cfg);

    expect_int("long gpu uuid set", cfg.gpu_uuid_set, 1);
    expect_int("long gpu uuid length", (int)strlen(cfg.gpu_uuid), NVFIX_GPU_UUID_MAX - 1);
    expect_int("long gpu uuid terminator", cfg.gpu_uuid[NVFIX_GPU_UUID_MAX - 1], '\0');
}

int main(void) {
    test_truthy_values();
    test_config_defaults();
    test_config_env_values();
    test_legacy_env_values_still_work();
    test_nvscope_env_takes_precedence();
    test_gpu_uuid_truncates_safely();

    if (failures != 0) {
        return 1;
    }

    return 0;
}
EOF

cc -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Isrc \
    tmp/test_env_detection.c src/env.c -o tmp/test_env_detection
tmp/test_env_detection
