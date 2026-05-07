#ifndef NVFIX_FD_MAP_H
#define NVFIX_FD_MAP_H

#define NVFIX_PATH_MAX 256
#define NVFIX_BUS_ID_MAX 32

enum nvfix_fd_kind {
    NVFIX_FD_OTHER,
    NVFIX_FD_NVIDIA_CTL,
    NVFIX_FD_NVIDIA_DEVICE,
    NVFIX_FD_NVIDIA_UVM,
    NVFIX_FD_PROC_GPUS_DIR,
    NVFIX_FD_PROC_GPU_INFO
};

struct nvfix_fd_record {
    enum nvfix_fd_kind kind;
    int device_minor;
    char path[NVFIX_PATH_MAX];
    char bus_id[NVFIX_BUS_ID_MAX];
};

int nvfix_classify_path(const char *path, struct nvfix_fd_record *record);
void nvfix_fd_map_clear(void);
/* The fd map intentionally tracks only descriptors 0 through 4095. */
void nvfix_fd_map_set(int fd, const struct nvfix_fd_record *record);
int nvfix_fd_map_get(int fd, struct nvfix_fd_record *record);
void nvfix_fd_map_remove(int fd);

#endif
