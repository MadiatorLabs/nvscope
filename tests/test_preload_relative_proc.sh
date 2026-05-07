#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p build/tests
cat >build/tests/test_preload_relative_proc.c <<'EOF'
#include "fd_map.h"

#include <stdio.h>
#include <string.h>

const char *nvfix_test_effective_openat_path(int dirfd, const char *path, char *buffer, size_t buffer_len);
int nvfix_test_readdir_entry_allowed(int dirfd, const char *name);
void nvfix_test_set_proc_filter_ready(int ready);
void nvfix_test_set_allowed_bus(const char *bus_id);

static int failures;

static void expect_string(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", name, got, want);
        failures++;
    }
}

static void expect_same_pointer(const char *name, const char *got, const char *want) {
    if (got != want) {
        fprintf(stderr, "%s: got %p want %p\n", name, (const void *)got, (const void *)want);
        failures++;
    }
}

int main(void) {
    struct nvfix_fd_record proc_gpus = {0};
    char buffer[128];
    const char *relative = "0000:05:00.0/information";
    const char *leading_dot = "./0000:05:00.0/information";
    const char *middle_dot = "0000:05:00.0/./information";
    const char *parent_dot = "0000:05:00.0/../information";
    const char *absolute = "/proc/driver/nvidia/gpus/0000:05:00.0/information";
    const char *unknown = "0000:06:00.0/information";
    const char *long_relative =
        "0000:05:00.0/information/"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    proc_gpus.kind = NVFIX_FD_PROC_GPUS_DIR;
    nvfix_fd_map_set(42, &proc_gpus);

    expect_string("relative proc info path",
                  nvfix_test_effective_openat_path(42, relative, buffer, sizeof(buffer)),
                  "/proc/driver/nvidia/gpus/0000:05:00.0/information");
    expect_string("leading dot proc info path",
                  nvfix_test_effective_openat_path(42, leading_dot, buffer, sizeof(buffer)),
                  "/proc/driver/nvidia/gpus/0000:05:00.0/information");
    expect_string("middle dot proc info path",
                  nvfix_test_effective_openat_path(42, middle_dot, buffer, sizeof(buffer)),
                  "/proc/driver/nvidia/gpus/0000:05:00.0/information");
    expect_same_pointer("parent segment unchanged",
                        nvfix_test_effective_openat_path(42, parent_dot, buffer, sizeof(buffer)),
                        parent_dot);
    expect_same_pointer("absolute path unchanged",
                        nvfix_test_effective_openat_path(42, absolute, buffer, sizeof(buffer)),
                        absolute);
    expect_same_pointer("unknown dirfd unchanged",
                        nvfix_test_effective_openat_path(43, unknown, buffer, sizeof(buffer)),
                        unknown);
    expect_same_pointer("overflow unchanged",
                        nvfix_test_effective_openat_path(42, long_relative, buffer, 32),
                        long_relative);

    nvfix_test_set_allowed_bus("0000:05:00.0");
    nvfix_test_set_proc_filter_ready(1);
    expect_string("dir dot allowed",
                  nvfix_test_readdir_entry_allowed(42, ".") ? "yes" : "no",
                  "yes");
    expect_string("dir dotdot allowed",
                  nvfix_test_readdir_entry_allowed(42, "..") ? "yes" : "no",
                  "yes");
    expect_string("dir allowed bus kept",
                  nvfix_test_readdir_entry_allowed(42, "0000:05:00.0") ? "yes" : "no",
                  "yes");
    expect_string("dir denied bus skipped",
                  nvfix_test_readdir_entry_allowed(42, "0000:06:00.0") ? "yes" : "no",
                  "no");
    expect_string("untracked dirfd pass through",
                  nvfix_test_readdir_entry_allowed(43, "0000:06:00.0") ? "yes" : "no",
                  "yes");
    nvfix_test_set_proc_filter_ready(0);
    expect_string("filter disabled pass through",
                  nvfix_test_readdir_entry_allowed(42, "0000:06:00.0") ? "yes" : "no",
                  "yes");

    if (failures != 0) {
        return 1;
    }
    return 0;
}
EOF

cc -std=c11 -D_GNU_SOURCE -DNVFIX_TESTING -Wall -Wextra -Werror -Isrc \
    build/tests/test_preload_relative_proc.c \
    src/preload.c src/env.c src/fd_map.c src/ioctl_filter.c src/ioctl_trace.c src/log.c src/proc_filter.c \
    -ldl -pthread -o build/tests/test_preload_relative_proc
build/tests/test_preload_relative_proc
