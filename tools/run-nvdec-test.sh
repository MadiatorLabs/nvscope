#!/bin/sh
set -eu

input="${1:-/tmp/cpu_h264_420.mp4}"

ffmpeg -hide_banner -loglevel verbose \
  -f lavfi -i testsrc=duration=3:size=1280x720:rate=30 \
  -pix_fmt yuv420p \
  -c:v libx264 \
  -y "$input"

ffmpeg -hide_banner -loglevel verbose \
  -hwaccel cuda -hwaccel_output_format cuda \
  -i "$input" \
  -f null -
