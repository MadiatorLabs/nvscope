# Technical Notes

`nvscope` is a Runpod-focused shim that wraps selected libc entry points with `LD_PRELOAD`:

- `open`, `open64`, `openat`, `openat64`
- `close`
- `read`
- `stat`, `fstat`, `newfstatat`
- `opendir`, `fdopendir`, `readdir`, `readdir64`
- `getdents64`
- `ioctl`

The procfs filter maps mounted `/dev/nvidiaN` nodes to NVIDIA procfs entries by reading `Device Minor` from:

```text
/proc/driver/nvidia/gpus/*/information
```

The RM ioctl filter uses NVIDIA Linux ioctl layouts from the 570 driver family:

- `NV_ESC_CARD_INFO`
- `NV_ESC_RM_CONTROL`
- `NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS`
- `NV0000_CTRL_CMD_GPU_GET_ACTIVE_DEVICE_IDS`

It learns allowed opaque RM `gpuId` values from `NV_ESC_CARD_INFO` entries whose `minor_number` matches an already-mounted physical device. It then compacts returned GPU ID arrays to the learned allowed IDs and invalidates the remaining entries with `0xffffffff`.

The implementation intentionally does not filter `GET_PROBED_IDS` or attach/detach commands. Those paths have different semantics and are not required for the validated Runpod NVENC/NVDEC failure.
