#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 GOOD_LOG BAD_LOG" >&2
    exit 2
fi

good_log="$1"
bad_log="$2"

test -f "$good_log"
test -f "$bad_log"

tmp_good="${TMPDIR:-/tmp}/nvscope-good-$$.txt"
tmp_bad="${TMPDIR:-/tmp}/nvscope-bad-$$.txt"
trap 'rm -f "$tmp_good" "$tmp_bad"' EXIT HUP INT TERM

section() {
    printf '\n== %s ==\n' "$1"
}

extract_topology() {
    log="$1"
    grep -E \
        '/dev/nvidia|nvidia-caps|Bus Location|GPU UUID|Device Minor|pci.bus_id|driver_version' \
        "$log" || true
}

section "Device node and proc topology differences"
extract_topology "$good_log" >"$tmp_good"
extract_topology "$bad_log" >"$tmp_bad"
diff -u "$tmp_good" "$tmp_bad" || true

section "Bad log NVENC/NVDEC failure lines"
grep -En \
    'NVENC|NVDEC|nvenc|nvdec|CUDA|cuda|cuvid|No capable devices|Cannot load|error|failed' \
    "$bad_log" || true
