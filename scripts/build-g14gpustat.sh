#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Builds g14-gpustat (NVIDIA GPU temperature, P-state, utilisation, clocks, video memory on NullMoth; no nvidia-smi on macOS)
# into build/g14-gpustat. `sudo bash scripts/build-g14gpustat.sh --install` (no rebuild as root) copies it to /usr/local/bin.
# Needs the RM headers of NullMoth's RM version in ~/ogkm615 (see scripts/build-g14hotkeys.sh), plus src/common/sdk/nvidia/inc
# /ctrl/ctrl2080 (part of the same sparse set).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build/g14-gpustat
if [ "${1:-}" != "--install" ]; then
O=${OGKM:-$HOME/ogkm615}
[ -d "$O/src/common/sdk/nvidia/inc" ] || { echo "no RM headers in $O (set OGKM)"; exit 1; }
mkdir -p build
clang -arch x86_64 -mmacosx-version-min=13.0 -O1 -Wall -Wno-unknown-pragmas -framework IOKit -Imacos/Common \
  macos/G14GpuStat/gpustat.c macos/Common/nvgpu.c -o "$BIN" \
  -I"$O/src/common/sdk/nvidia/inc" -I"$O/src/common/sdk/nvidia/inc/class" -I"$O/src/common/sdk/nvidia/inc/ctrl" \
  -I"$O/src/nvidia/arch/nvalloc/unix/include" -I"$O/src/common/inc" -I"$O/kernel-open/common/inc" -I"$O/src/nvidia/inc/kernel"
codesign --force --sign - --identifier com.itlmt7925.g14-gpustat "$BIN" >/dev/null
echo "built $BIN"
else
  [ "$(id -u)" = 0 ] || { echo "--install needs sudo"; exit 1; }
  [ -x "$BIN" ] || { echo "no $BIN: build it first without sudo"; exit 1; }
  install -m 755 -o root -g wheel "$BIN" /usr/local/bin/g14-gpustat
  echo "installed /usr/local/bin/g14-gpustat"
fi
