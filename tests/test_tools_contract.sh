#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

require_file() {
    path="$1"
    test -f "$path"
    test -x "$path"
    sh -n "$path"
}

require_grep() {
    pattern="$1"
    path="$2"
    grep -F -- "$pattern" "$path" >/dev/null
}

require_file tools/run-nvenc-test.sh
require_grep "set -eu" tools/run-nvenc-test.sh
require_grep "output=\"\${1:-/tmp/nvenc_h264_test.mp4}\"" tools/run-nvenc-test.sh
require_grep "-c:v h264_nvenc" tools/run-nvenc-test.sh
require_grep "-y \"\$output\"" tools/run-nvenc-test.sh

require_file tools/run-nvdec-test.sh
require_grep "set -eu" tools/run-nvdec-test.sh
require_grep "input=\"\${1:-/tmp/cpu_h264_420.mp4}\"" tools/run-nvdec-test.sh
require_grep "-pix_fmt yuv420p" tools/run-nvdec-test.sh
require_grep "-c:v libx264" tools/run-nvdec-test.sh
require_grep "-hwaccel cuda -hwaccel_output_format cuda" tools/run-nvdec-test.sh
require_grep "-f null -" tools/run-nvdec-test.sh

require_file tools/nvscope
require_grep "NVSCOPE_IOCTL=1" tools/nvscope
require_grep "libnvscope.so" tools/nvscope

require_file tools/nvscope-probe
require_grep "set -u" tools/nvscope-probe
require_grep "nvidia-smi --query-gpu=index,uuid,name,driver_version,pci.bus_id --format=csv" tools/nvscope-probe
require_grep "/proc/driver/nvidia/version" tools/nvscope-probe
require_grep "find /dev/nvidia-caps" tools/nvscope-probe
require_grep "Bus Location\\|GPU UUID\\|Device Minor" tools/nvscope-probe
require_grep "env | grep -E '^(NVIDIA|CUDA|NVSCOPE|NV_VIDEO_FIX)_'" tools/nvscope-probe
require_grep "ldconfig -p" tools/nvscope-probe
require_grep "h264_nvenc" tools/nvscope-probe

require_file tools/compare-good-bad.sh
require_grep 'usage: $0 GOOD_LOG BAD_LOG' tools/compare-good-bad.sh
require_grep "NVENC|NVDEC|nvenc|nvdec|CUDA|cuda|cuvid|No capable devices|Cannot load|error|failed" tools/compare-good-bad.sh
