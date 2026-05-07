# nvscope

`nvscope` is a Runpod-focused Linux `LD_PRELOAD` shim for NVIDIA GPU containers where CUDA works, but NVENC or NVDEC fails because the NVIDIA video stack sees host-global GPU topology instead of the GPU devices assigned to the pod.

It is built for Runpod users, template authors, and image maintainers who need video encode/decode workloads to behave consistently inside assigned-GPU containers.

It targets Runpod pod failures such as:

```text
OpenEncodeSessionEx failed: unsupported device (2)
No capable devices found
CUDA_ERROR_NO_DEVICE
```

The short version:

```sh
make
tools/nvscope -- ffmpeg -hide_banner -f lavfi -i testsrc=duration=3:size=1280x720:rate=30 -c:v h264_nvenc /tmp/nvenc-test.mp4
```

## Who It Is For

- Runpod users whose pod can run CUDA workloads but cannot open NVENC or NVDEC
- Runpod template maintainers who want a small runtime fix in their Docker images
- developers shipping FFmpeg, ComfyUI, video inference, streaming, or batch transcode workloads on Runpod GPUs

## What It Does

`nvscope` makes NVIDIA video userspace see a container-local GPU topology:

- filters `/proc/driver/nvidia/gpus` to mounted `/dev/nvidiaN` devices
- optionally filters NVIDIA Resource Manager GPU ID lists with `NVSCOPE_IOCTL=1`
- logs NVIDIA device/proc/ioctl activity with `NVSCOPE_TRACE=1`

It only filters visibility downward. It does not create device nodes, bypass cgroups, add GPU IDs, or make unmounted GPUs usable.

## Install From Source

Build:

```sh
make
```

Install:

```sh
make install
```

This installs:

```text
/usr/local/bin/nvscope
/usr/local/bin/nvscope-probe
/usr/local/lib/nvscope/libnvscope.so
```

Use a custom prefix if needed:

```sh
make install PREFIX=/usr
```

## Usage

Run a command through the wrapper:

```sh
nvscope -- ffmpeg -i input.mp4 -c:v h264_nvenc output.mp4
```

Enable trace logs:

```sh
nvscope --trace -- ffmpeg -i input.mp4 -c:v h264_nvenc output.mp4
```

Run a diagnostic probe:

```sh
nvscope-probe 2>&1 | tee /tmp/nvscope-probe.log
```

The wrapper enables experimental RM ioctl filtering for the wrapped process by default. To run procfs filtering only:

```sh
nvscope --no-ioctl -- ffmpeg ...
```

## Environment Variables

Advanced users can call the library directly:

```sh
NVSCOPE_IOCTL=1 LD_PRELOAD="$PWD/libnvscope.so" ffmpeg ...
```

Supported variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `NVSCOPE_TRACE=1` | off | print concise trace logs to stderr |
| `NVSCOPE_PROC=0` | on | disable procfs topology filtering |
| `NVSCOPE_IOCTL=1` | off for raw `LD_PRELOAD`, on in `nvscope` wrapper | enable experimental RM ioctl topology filtering |
| `NVSCOPE_GPU_UUID=GPU-...` | unset | restrict filtering to one already-assigned GPU UUID |

The older `NV_VIDEO_FIX_*` variables are still accepted as compatibility aliases.

## Runpod Docker Images

Build in one layer and copy the tiny runtime bits into your Runpod image:

```dockerfile
FROM ubuntu:24.04 AS nvscope-build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential ca-certificates
WORKDIR /src/nvscope
COPY . .
RUN make

FROM your-runpod-or-ffmpeg-image
COPY --from=nvscope-build /src/nvscope/libnvscope.so /usr/local/lib/nvscope/libnvscope.so
COPY --from=nvscope-build /src/nvscope/tools/nvscope /usr/local/bin/nvscope
COPY --from=nvscope-build /src/nvscope/tools/nvscope-probe /usr/local/bin/nvscope-probe
```

Then:

```sh
nvscope -- ffmpeg ...
```

## Safety Model

`nvscope` is an isolation consistency tool, not an access bypass. It never:

- creates `/dev/nvidia*` nodes
- rewrites topology to include more GPUs than the container already has
- makes `nvidia-smi` show extra GPUs
- bypasses cgroups, mounts, or Linux permissions

If the container is missing NVIDIA driver libraries, `/dev/nvidiactl`, `/dev/nvidia-uvm`, or the assigned physical GPU device node, `nvscope` cannot repair that setup.

## Documentation

- [Safety model](docs/safety-model.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Technical notes](docs/technical-notes.md)
- [Packaging](docs/packaging.md)

## License

Apache License 2.0. Copyright 2026 nvscope contributors. See [LICENSE](LICENSE).
