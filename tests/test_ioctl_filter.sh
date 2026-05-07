#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p build/tests
cat >build/tests/test_ioctl_filter.c <<'EOF'
#include "env.h"
#include "ioctl_filter.h"
#include "nvidia_rm.h"
#include "proc_filter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", name, got, want);
        failures++;
    }
}

static void expect_u32(const char *name, uint32_t got, uint32_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got 0x%x want 0x%x\n", name, got, want);
        failures++;
    }
}

static struct nvfix_config test_config(int ioctl_enabled) {
    struct nvfix_config cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.ioctl_filter_enabled = ioctl_enabled;
    return cfg;
}

static struct nvfix_allowed_gpus allowed_minor_2(void) {
    struct nvfix_allowed_gpus allowed;

    memset(&allowed, 0, sizeof(allowed));
    allowed.valid = 1;
    allowed.count = 1;
    allowed.gpus[0].present = 1;
    allowed.gpus[0].device_minor = 2;
    strcpy(allowed.gpus[0].bus_id, "0000:01:00.0");
    strcpy(allowed.gpus[0].uuid, "GPU-allowed");
    return allowed;
}

static void test_card_info_records_and_hides_disallowed_minors(void) {
    struct nvfix_config cfg = test_config(1);
    struct nvfix_allowed_gpus allowed = allowed_minor_2();
    struct nvfix_nv_ioctl_card_info cards[NVFIX_NV_MAX_DEVICES];

    nvfix_test_ioctl_filter_reset();
    memset(cards, 0, sizeof(cards));

    cards[0].valid = 1;
    cards[0].gpu_id = 0xaaaa0002U;
    cards[0].minor_number = 2;
    cards[1].valid = 1;
    cards[1].gpu_id = 0xbbbb0003U;
    cards[1].minor_number = 3;

    nvfix_ioctl_filter_after(&cfg, &allowed, NVFIX_IOCTL_CARD_INFO_REQUEST, cards, 0);

    expect_int("allowed card remains valid", cards[0].valid, 1);
    expect_int("disallowed card hidden", cards[1].valid, 0);
    expect_int("one allowed gpu id learned", nvfix_test_ioctl_allowed_gpu_id_count(), 1);
}

static void test_attached_ids_are_filtered_to_learned_allowed_gpu_ids(void) {
    struct nvfix_config cfg = test_config(1);
    struct nvfix_allowed_gpus allowed = allowed_minor_2();
    struct nvfix_nv_ioctl_card_info cards[NVFIX_NV_MAX_DEVICES];
    uint32_t gpu_ids[NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS];
    struct nvfix_nvos54_parameters rm;
    size_t i;

    nvfix_test_ioctl_filter_reset();
    memset(cards, 0, sizeof(cards));
    cards[0].valid = 1;
    cards[0].gpu_id = 0xaaaa0002U;
    cards[0].minor_number = 2;
    cards[1].valid = 1;
    cards[1].gpu_id = 0xbbbb0003U;
    cards[1].minor_number = 3;
    nvfix_ioctl_filter_after(&cfg, &allowed, NVFIX_IOCTL_CARD_INFO_REQUEST, cards, 0);

    for (i = 0; i < NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) {
        gpu_ids[i] = NVFIX_NV0000_CTRL_GPU_INVALID_ID;
    }
    gpu_ids[0] = 0xaaaa0002U;
    gpu_ids[1] = 0xbbbb0003U;

    memset(&rm, 0, sizeof(rm));
    rm.cmd = NVFIX_NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS;
    rm.params = (uint64_t)(uintptr_t)gpu_ids;
    rm.paramsSize = sizeof(gpu_ids);
    rm.status = 0;

    nvfix_ioctl_filter_after(&cfg, &allowed, NVFIX_IOCTL_RM_CONTROL_REQUEST, &rm, 0);

    expect_u32("first id kept", gpu_ids[0], 0xaaaa0002U);
    expect_u32("second id invalidated", gpu_ids[1], NVFIX_NV0000_CTRL_GPU_INVALID_ID);
    expect_u32("third id invalid", gpu_ids[2], NVFIX_NV0000_CTRL_GPU_INVALID_ID);
}

static void test_attached_ids_are_unchanged_without_ioctl_flag(void) {
    struct nvfix_config cfg = test_config(0);
    struct nvfix_allowed_gpus allowed = allowed_minor_2();
    struct nvfix_nv_ioctl_card_info cards[NVFIX_NV_MAX_DEVICES];
    uint32_t gpu_ids[NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS];
    struct nvfix_nvos54_parameters rm;
    size_t i;

    nvfix_test_ioctl_filter_reset();
    memset(cards, 0, sizeof(cards));
    cards[0].valid = 1;
    cards[0].gpu_id = 0xaaaa0002U;
    cards[0].minor_number = 2;
    nvfix_ioctl_filter_after(&cfg, &allowed, NVFIX_IOCTL_CARD_INFO_REQUEST, cards, 0);

    for (i = 0; i < NVFIX_NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) {
        gpu_ids[i] = NVFIX_NV0000_CTRL_GPU_INVALID_ID;
    }
    gpu_ids[0] = 0xaaaa0002U;
    gpu_ids[1] = 0xbbbb0003U;

    memset(&rm, 0, sizeof(rm));
    rm.cmd = NVFIX_NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS;
    rm.params = (uint64_t)(uintptr_t)gpu_ids;
    rm.paramsSize = sizeof(gpu_ids);
    rm.status = 0;

    nvfix_ioctl_filter_after(&cfg, &allowed, NVFIX_IOCTL_RM_CONTROL_REQUEST, &rm, 0);

    expect_u32("first id unchanged", gpu_ids[0], 0xaaaa0002U);
    expect_u32("second id unchanged", gpu_ids[1], 0xbbbb0003U);
}

int main(void) {
    test_card_info_records_and_hides_disallowed_minors();
    test_attached_ids_are_filtered_to_learned_allowed_gpu_ids();
    test_attached_ids_are_unchanged_without_ioctl_flag();

    return failures == 0 ? 0 : 1;
}
EOF

cc -std=c11 -D_GNU_SOURCE -DNVFIX_TESTING -Wall -Wextra -Werror \
    -Isrc build/tests/test_ioctl_filter.c src/ioctl_filter.c src/log.c \
    -pthread -o build/tests/test_ioctl_filter
build/tests/test_ioctl_filter
