#!/bin/sh
set -eu

output="${1:-/tmp/nvenc_h264_test.mp4}"

ffmpeg -hide_banner -loglevel verbose \
  -f lavfi -i testsrc=duration=3:size=1280x720:rate=30 \
  -c:v h264_nvenc \
  -y "$output"
