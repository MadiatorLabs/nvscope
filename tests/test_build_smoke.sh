#!/bin/sh
set -eu

root_dir="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

make clean >/dev/null 2>&1 || true
make >/tmp/nvscope-build.log 2>&1

test -f libnvscope.so
file libnvscope.so | grep -E 'shared object|dynamically linked' >/dev/null
