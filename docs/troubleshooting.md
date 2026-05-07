# Troubleshooting

## Check The Runpod Shape

Run:

```sh
nvscope-probe 2>&1 | tee /tmp/nvscope-probe.log
```

The most useful signs are:

- `nvidia-smi` shows one assigned GPU
- `/dev/nvidiaN` shows one physical GPU node
- `/proc/driver/nvidia/gpus` shows more GPUs than the container should have
- NVENC fails with `OpenEncodeSessionEx failed: unsupported device (2)`
- CUVID/NVDEC fails with `CUDA_ERROR_NO_DEVICE`

## Use The Wrapper First In The Pod

```sh
nvscope --trace -- ffmpeg ...
```

Look for:

```text
[nvscope] filtered RM attached gpuIds to 1 allowed entries
```

If that line does not appear, the workload may not be using the RM commands `nvscope` currently filters, or card-info did not provide a matching mounted GPU minor.

## Try Procfs-Only Mode

```sh
nvscope --no-ioctl --trace -- ffmpeg ...
```

If procfs-only mode works, your failure is likely caused by `/proc/driver/nvidia/gpus` exposure. If procfs-only mode fails but default `nvscope` works, your failure likely also needs RM ioctl topology filtering.

## Missing Devices

`nvscope` cannot repair missing device mounts. Check for:

```sh
ls -l /dev/nvidiactl /dev/nvidia-uvm /dev/nvidia*
```

At minimum, most NVENC/NVDEC workloads need `nvidiactl`, `nvidia-uvm`, and the assigned physical `/dev/nvidiaN` node.
