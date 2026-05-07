# Safety Model

`nvscope` only makes NVIDIA userspace see a topology consistent with the GPU devices already mounted into the Runpod container.

It can hide unrelated host GPUs from selected discovery paths:

- `/proc/driver/nvidia/gpus`
- `NV_ESC_CARD_INFO`
- RM control responses for attached and active GPU IDs

It cannot and will not:

- create `/dev/nvidia*` device nodes
- mount driver libraries
- bypass cgroups
- make unassigned GPUs available
- make `nvidia-smi` show extra GPUs
- add GPU IDs that were not returned by the NVIDIA driver

If topology detection is ambiguous, the shim prefers pass-through behavior except for procfs paths that are confidently disallowed.

The RM ioctl filter is guarded by `NVSCOPE_IOCTL=1`. The `nvscope` wrapper enables it for the wrapped command because the known Runpod failure requires it, but raw `LD_PRELOAD` usage leaves it disabled by default.
