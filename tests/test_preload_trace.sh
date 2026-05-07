#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

mkdir -p build/tests
make >/dev/null

log_file="build/tests/preload-trace.log"
NVSCOPE_TRACE=1 \
LD_PRELOAD="$root_dir/libnvscope.so" \
sh -c '
    cat /dev/nvidiactl >/dev/null
    cat /proc/driver/nvidia/gpus >/dev/null
' 2>"$log_file" || true

grep -F "[nvscope] loaded trace=1" "$log_file" >/dev/null
grep -F "[nvscope] open /dev/nvidiactl" "$log_file" >/dev/null
grep -F "[nvscope] open /proc/driver/nvidia/gpus" "$log_file" >/dev/null
