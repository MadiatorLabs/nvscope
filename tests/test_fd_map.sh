#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p build/tests
cat >build/tests/test_fd_map.c <<'EOF'
#include "fd_map.h"

#include <limits.h>
#include <stdio.h>
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

static void expect_kind(const char *label,
                        const char *path,
                        int want_classified,
                        enum nvfix_fd_kind want_kind,
                        int want_minor,
                        const char *want_bus,
                        const char *want_path) {
    struct nvfix_fd_record record;
    int classified;

    classified = nvfix_classify_path(path, &record);
    expect_int(label, classified, want_classified);
    expect_int("kind", record.kind, want_kind);
    expect_int("device minor", record.device_minor, want_minor);
    expect_string("bus id", record.bus_id, want_bus);
    expect_string("path", record.path, want_path);
}

static void test_path_classification(void) {
    expect_kind("/dev/nvidiactl", "/dev/nvidiactl", 1, NVFIX_FD_NVIDIA_CTL, -1, "", "/dev/nvidiactl");
    expect_kind("/dev/nvidia1", "/dev/nvidia1", 1, NVFIX_FD_NVIDIA_DEVICE, 1, "", "/dev/nvidia1");
    expect_kind("/dev/nvidia-uvm", "/dev/nvidia-uvm", 1, NVFIX_FD_NVIDIA_UVM, -1, "", "/dev/nvidia-uvm");
    expect_kind("/proc/driver/nvidia/gpus", "/proc/driver/nvidia/gpus", 1, NVFIX_FD_PROC_GPUS_DIR, -1, "", "/proc/driver/nvidia/gpus");
    expect_kind("/proc gpu information",
                "/proc/driver/nvidia/gpus/0000:04:00.0/information",
                1,
                NVFIX_FD_PROC_GPU_INFO,
                -1,
                "0000:04:00.0",
                "/proc/driver/nvidia/gpus/0000:04:00.0/information");
    expect_kind("/tmp/not-nvidia", "/tmp/not-nvidia", 0, NVFIX_FD_OTHER, -1, "", "/tmp/not-nvidia");
}

static void test_classification_edge_cases(void) {
    char overflow_path[64];
    char long_bus[NVFIX_BUS_ID_MAX + 1];
    char long_bus_path[128];
    size_t i;

    expect_kind("NULL path", NULL, 0, NVFIX_FD_OTHER, -1, "", "");
    expect_kind("invalid nvidia suffix", "/dev/nvidia1x", 0, NVFIX_FD_OTHER, -1, "", "/dev/nvidia1x");

    snprintf(overflow_path, sizeof(overflow_path), "/dev/nvidia%ld", (long)INT_MAX + 1L);
    expect_kind("minor overflow", overflow_path, 0, NVFIX_FD_OTHER, -1, "", overflow_path);

    for (i = 0; i < NVFIX_BUS_ID_MAX; i++) {
        long_bus[i] = 'A';
    }
    long_bus[NVFIX_BUS_ID_MAX] = '\0';
    snprintf(long_bus_path,
             sizeof(long_bus_path),
             "/proc/driver/nvidia/gpus/%s/information",
             long_bus);
    expect_kind("overlong proc bus id", long_bus_path, 0, NVFIX_FD_OTHER, -1, "", long_bus_path);
}

static void test_fd_map_set_get_remove(void) {
    struct nvfix_fd_record record;
    struct nvfix_fd_record replacement;
    struct nvfix_fd_record got;

    nvfix_fd_map_clear();
    expect_int("missing fd", nvfix_fd_map_get(42, &got), 0);

    nvfix_classify_path("/dev/nvidia1", &record);
    nvfix_fd_map_set(42, &record);

    expect_int("get fd", nvfix_fd_map_get(42, &got), 1);
    expect_int("get kind", got.kind, NVFIX_FD_NVIDIA_DEVICE);
    expect_int("get minor", got.device_minor, 1);
    expect_string("get path", got.path, "/dev/nvidia1");

    nvfix_classify_path("/dev/nvidia-uvm", &replacement);
    nvfix_fd_map_set(42, &replacement);
    expect_int("get replacement fd", nvfix_fd_map_get(42, &got), 1);
    expect_int("get replacement kind", got.kind, NVFIX_FD_NVIDIA_UVM);
    expect_string("get replacement path", got.path, "/dev/nvidia-uvm");

    nvfix_fd_map_remove(42);
    expect_int("removed fd", nvfix_fd_map_get(42, &got), 0);
}

static void test_fd_map_bounds(void) {
    struct nvfix_fd_record record;
    struct nvfix_fd_record got;

    nvfix_fd_map_clear();
    nvfix_classify_path("/dev/nvidia1", &record);

    nvfix_fd_map_set(-1, &record);
    nvfix_fd_map_set(4096, &record);
    expect_int("negative fd ignored", nvfix_fd_map_get(-1, &got), 0);
    expect_int("fd 4096 ignored", nvfix_fd_map_get(4096, &got), 0);

    nvfix_fd_map_set(4095, &record);
    expect_int("fd 4095 tracked", nvfix_fd_map_get(4095, &got), 1);
    nvfix_fd_map_remove(4095);
    expect_int("fd 4095 removed", nvfix_fd_map_get(4095, &got), 0);

    nvfix_fd_map_remove(-1);
    nvfix_fd_map_remove(4096);
}

int main(void) {
    test_path_classification();
    test_classification_edge_cases();
    test_fd_map_set_get_remove();
    test_fd_map_bounds();

    if (failures != 0) {
        return 1;
    }

    return 0;
}
EOF

cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc build/tests/test_fd_map.c src/fd_map.c -pthread -o build/tests/test_fd_map
build/tests/test_fd_map
