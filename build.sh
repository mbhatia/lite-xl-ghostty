#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Ninja}"

if [[ "${1:-}" == "clean" ]]; then
  cmake --build "$BUILD_DIR" --target clean 2>/dev/null || true
  rm -f libraries/ghostty_lxl/init.so libraries/ghostty_lxl/init.dylib libraries/ghostty_lxl/init.lib
  rm -f libraries/libghostty_lxl.so libraries/libghostty_lxl.dylib libraries/libghostty_lxl.lib
  rm -f plugins/ghostty/libghostty_lxl.so plugins/ghostty/libghostty_lxl.dylib plugins/ghostty/libghostty_lxl.lib
  exit 0
fi

cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  "$@"

cmake --build "$BUILD_DIR"
