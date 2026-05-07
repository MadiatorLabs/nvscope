#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p build/tests
cat >build/tests/test_proc_filter.c <<'EOF'
#include "proc_filter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct linux_dirent64_test {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static int failures;

static void expect_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", name, got, want);
        failures++;
    }
}

static void expect_size(const char *name, size_t got, size_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %lu want %lu\n", name, (unsigned long)got, (unsigned long)want);
        failures++;
    }
}

static void expect_string(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", name, got, want);
        failures++;
    }
}

static void test_parse_gpu_information(void) {
    const char first[] =
        "Model: NVIDIA Test\n"
        "GPU UUID: GPU-first-uuid\n"
        "Bus Location: 0000:04:00.0\n"
        "Device Minor: 1\n";
    const char second[] =
        "GPU UUID: GPU-second-uuid\n"
        "Bus Location: 0000:05:00.0\n"
        "Device Minor: 2\n";
    struct nvfix_gpu_info info;

    expect_int("parse first",
               nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:04:00.0/information",
                                           first,
                                           &info),
               1);
    expect_int("first present", info.present, 1);
    expect_int("first minor", info.device_minor, 1);
    expect_string("first bus", info.bus_id, "0000:04:00.0");
    expect_string("first uuid", info.uuid, "GPU-first-uuid");

    expect_int("parse second",
               nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:05:00.0/information",
                                           second,
                                           &info),
               1);
    expect_int("second present", info.present, 1);
    expect_int("second minor", info.device_minor, 2);
    expect_string("second bus", info.bus_id, "0000:05:00.0");
    expect_string("second uuid", info.uuid, "GPU-second-uuid");
}

static void test_parse_gpu_information_edges(void) {
    char overlong_bus[NVFIX_PROC_BUS_ID_MAX + 1];
    char overlong_text[160];
    const char overflow_minor[] =
        "GPU UUID: GPU-overflow\n"
        "Bus Location: 0000:06:00.0\n"
        "Device Minor: 2147483648\n";
    struct nvfix_gpu_info info;
    size_t i;

    for (i = 0; i < NVFIX_PROC_BUS_ID_MAX; i++) {
        overlong_bus[i] = 'A';
    }
    overlong_bus[NVFIX_PROC_BUS_ID_MAX] = '\0';
    snprintf(overlong_text,
             sizeof(overlong_text),
             "GPU UUID: GPU-overlong\nBus Location: %s\nDevice Minor: 3\n",
             overlong_bus);

    expect_int("reject overlong bus",
               nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:06:00.0/information",
                                           overlong_text,
                                           &info),
               0);
    expect_int("reject minor overflow",
               nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:06:00.0/information",
                                           overflow_minor,
                                           &info),
               0);
}

static void test_parse_gpu_information_path_fallback(void) {
    const char text[] =
        "GPU UUID: GPU-fallback-uuid\n"
        "Device Minor: 7\n";
    struct nvfix_gpu_info info;

    expect_int("parse bus from exact path",
               nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:07:00.0/information",
                                           text,
                                           &info),
               1);
    expect_string("fallback bus", info.bus_id, "0000:07:00.0");
    expect_int("fallback minor", info.device_minor, 7);
}

static void fill_infos(struct nvfix_gpu_info infos[2]) {
    const char first[] =
        "GPU UUID: GPU-first-uuid\n"
        "Bus Location: 0000:04:00.0\n"
        "Device Minor: 1\n";
    const char second[] =
        "GPU UUID: GPU-second-uuid\n"
        "Bus Location: 0000:05:00.0\n"
        "Device Minor: 2\n";

    nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:04:00.0/information",
                                first,
                                &infos[0]);
    nvfix_parse_gpu_information("/proc/driver/nvidia/gpus/0000:05:00.0/information",
                                second,
                                &infos[1]);
}

static void test_build_allowed_gpus(void) {
    struct nvfix_gpu_info infos[2];
    struct nvfix_allowed_gpus allowed;
    int minor_one[] = {1};
    int minor_both[] = {1, 2};

    fill_infos(infos);

    expect_int("minor one build",
               nvfix_build_allowed_gpus(infos, 2, minor_one, 1, NULL, &allowed),
               1);
    expect_int("minor one valid", allowed.valid, 1);
    expect_size("minor one count", allowed.count, 1);
    expect_string("minor one bus", allowed.gpus[0].bus_id, "0000:04:00.0");
    expect_int("minor one has first", nvfix_allowed_contains_bus(&allowed, "0000:04:00.0"), 1);
    expect_int("minor one lacks second", nvfix_allowed_contains_bus(&allowed, "0000:05:00.0"), 0);

    expect_int("minor both build",
               nvfix_build_allowed_gpus(infos, 2, minor_both, 2, NULL, &allowed),
               1);
    expect_int("minor both valid", allowed.valid, 1);
    expect_size("minor both count", allowed.count, 2);
    expect_int("minor both has first", nvfix_allowed_contains_bus(&allowed, "0000:04:00.0"), 1);
    expect_int("minor both has second", nvfix_allowed_contains_bus(&allowed, "0000:05:00.0"), 1);

    expect_int("uuid build",
               nvfix_build_allowed_gpus(infos, 2, minor_both, 2, "GPU-first-uuid", &allowed),
               1);
    expect_int("uuid valid", allowed.valid, 1);
    expect_size("uuid count", allowed.count, 1);
    expect_string("uuid bus", allowed.gpus[0].bus_id, "0000:04:00.0");
    expect_int("uuid has first", nvfix_allowed_contains_bus(&allowed, "0000:04:00.0"), 1);
    expect_int("uuid lacks second", nvfix_allowed_contains_bus(&allowed, "0000:05:00.0"), 0);
}

static void test_proc_path_allowed(void) {
    struct nvfix_gpu_info infos[2];
    struct nvfix_allowed_gpus allowed;
    int minor_one[] = {1};

    fill_infos(infos);
    nvfix_build_allowed_gpus(infos, 2, minor_one, 1, NULL, &allowed);

    expect_int("gpus dir allowed",
               nvfix_proc_path_allowed(&allowed, "/proc/driver/nvidia/gpus"),
               NVFIX_PROC_ALLOW);
    expect_int("allowed info path",
               nvfix_proc_path_allowed(&allowed, "/proc/driver/nvidia/gpus/0000:04:00.0/information"),
               NVFIX_PROC_ALLOW);
    expect_int("denied info path",
               nvfix_proc_path_allowed(&allowed, "/proc/driver/nvidia/gpus/0000:05:00.0/information"),
               NVFIX_PROC_DENY);
    expect_int("unrelated ignored",
               nvfix_proc_path_allowed(&allowed, "/tmp/not-nvidia"),
               NVFIX_PROC_IGNORE);
    memset(&allowed, 0, sizeof(allowed));
    expect_int("invalid allowed info pass through",
               nvfix_proc_path_allowed(&allowed, "/proc/driver/nvidia/gpus/0000:05:00.0/information"),
               NVFIX_PROC_ALLOW);
}

static size_t append_dirent(char *buffer, size_t offset, const char *name) {
    struct linux_dirent64_test *entry = (struct linux_dirent64_test *)(void *)(buffer + offset);
    size_t name_len = strlen(name) + 1;
    size_t reclen = sizeof(*entry) + name_len;

    reclen = (reclen + 7u) & ~7u;
    entry->d_ino = offset + 1u;
    entry->d_off = (int64_t)(offset + reclen);
    entry->d_reclen = (unsigned short)reclen;
    entry->d_type = 0;
    memcpy(entry->d_name, name, name_len);
    memset(entry->d_name + name_len, 0, reclen - sizeof(*entry) - name_len);

    return offset + reclen;
}

static int buffer_has_name(const char *buffer, ssize_t len, const char *name) {
    ssize_t offset = 0;

    while (offset < len) {
        const struct linux_dirent64_test *entry =
            (const struct linux_dirent64_test *)(const void *)(buffer + offset);
        if (strcmp(entry->d_name, name) == 0) {
            return 1;
        }
        offset += entry->d_reclen;
    }

    return 0;
}

static void test_filter_dirent64_buffer(void) {
    struct nvfix_gpu_info infos[2];
    struct nvfix_allowed_gpus allowed;
    int minor_one[] = {1};
    char buffer[512];
    char short_buffer[sizeof(struct linux_dirent64_test)];
    ssize_t short_len = (ssize_t)offsetof(struct linux_dirent64_test, d_name) - 1;
    size_t len = 0;
    ssize_t filtered;

    fill_infos(infos);
    nvfix_build_allowed_gpus(infos, 2, minor_one, 1, NULL, &allowed);

    len = append_dirent(buffer, len, ".");
    len = append_dirent(buffer, len, "..");
    len = append_dirent(buffer, len, "0000:04:00.0");
    len = append_dirent(buffer, len, "0000:05:00.0");

    filtered = nvfix_filter_dirent64_buffer(buffer, (ssize_t)len, &allowed);
    expect_int("keeps dot", buffer_has_name(buffer, filtered, "."), 1);
    expect_int("keeps dotdot", buffer_has_name(buffer, filtered, ".."), 1);
    expect_int("keeps allowed bus", buffer_has_name(buffer, filtered, "0000:04:00.0"), 1);
    expect_int("removes denied bus", buffer_has_name(buffer, filtered, "0000:05:00.0"), 0);

    memset(short_buffer, 0, sizeof(short_buffer));
    expect_int("short invalid buffer unchanged",
               (int)nvfix_filter_dirent64_buffer(short_buffer, short_len, &allowed),
               (int)short_len);
}

int main(void) {
    test_parse_gpu_information();
    test_parse_gpu_information_edges();
    test_parse_gpu_information_path_fallback();
    test_build_allowed_gpus();
    test_proc_path_allowed();
    test_filter_dirent64_buffer();

    if (failures != 0) {
        return 1;
    }

    return 0;
}
EOF

cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc build/tests/test_proc_filter.c src/proc_filter.c -o build/tests/test_proc_filter
build/tests/test_proc_filter
