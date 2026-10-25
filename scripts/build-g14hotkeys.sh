#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Builds G14Hotkeys (Fn keys sent as N-KEY HID reports: keyboard light, AURA / Slash, mic and speaker mute; OLED brightness
# through the system brightness keys, else over DPCD) into build/G14Hotkeys. Then
# `sudo bash scripts/build-g14hotkeys.sh --install` (no rebuild as root) copies it to /usr/local/bin and starts it at
# every boot as a root LaunchDaemon (restarted if it exits). Every new build needs Input Monitoring and Accessibility again
# (Accessibility: posting the brightness keys): remove /usr/local/bin/g14-hotkeys in both lists and add it back (ad-hoc
# signature, the grant follows the binary; AMFI off means macOS never asks).
# Log: /var/log/g14-hotkeys.log. Remove: sudo launchctl bootout system/com.itlmt7925.G14Hotkeys
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build/G14Hotkeys
if [ "${1:-}" != "--install" ]; then
# edpbl.c needs the RM headers of NullMoth's RM version: git clone --depth 1 --branch 615.78.08 --filter=blob:none --sparse
# https://github.com/NVIDIA/open-gpu-kernel-modules ~/ogkm615 (sparse set: src/common/sdk/nvidia/inc src/common/inc
# src/nvidia/arch/nvalloc/unix/include kernel-open/common/inc src/nvidia/inc/kernel)
O=${OGKM:-$HOME/ogkm615}
[ -d "$O/src/common/sdk/nvidia/inc" ] || { echo "no RM headers in $O (set OGKM)"; exit 1; }
mkdir -p build
clang -arch x86_64 -mmacosx-version-min=13.0 -O1 -Wno-everything -c macos/G14Hotkeys/edpbl.c -o build/edpbl.o \
  -I"$O/src/common/sdk/nvidia/inc" -I"$O/src/common/sdk/nvidia/inc/class" -I"$O/src/common/sdk/nvidia/inc/ctrl" \
  -I"$O/src/nvidia/arch/nvalloc/unix/include" -I"$O/src/common/inc" -I"$O/kernel-open/common/inc" -I"$O/src/nvidia/inc/kernel"
swiftc -O -target x86_64-apple-macos13.0 -import-objc-header macos/G14Hotkeys/edpbl.h macos/G14Hotkeys/G14Hotkeys.swift \
  build/edpbl.o -o "$BIN"
codesign --force --sign - --identifier com.itlmt7925.G14Hotkeys "$BIN" >/dev/null
echo "built $BIN"
else
  [ "$(id -u)" = 0 ] || { echo "--install needs sudo"; exit 1; }
  [ -x "$BIN" ] || { echo "no $BIN: build it first without sudo"; exit 1; }
  LD=/Library/LaunchDaemons/com.itlmt7925.G14Hotkeys.plist
  launchctl bootout system/com.itlmt7925.G14Hotkeys 2>/dev/null || true
  install -m 755 -o root -g wheel "$BIN" /usr/local/bin/g14-hotkeys
  /usr/bin/python3 -I - "$LD" <<'PY'
import plistlib, sys
plistlib.dump({"Label": "com.itlmt7925.G14Hotkeys", "ProgramArguments": ["/usr/local/bin/g14-hotkeys"],
               "RunAtLoad": True, "KeepAlive": True, "ThrottleInterval": 30,
               "StandardOutPath": "/var/log/g14-hotkeys.log", "StandardErrorPath": "/var/log/g14-hotkeys.log"},
              open(sys.argv[1], "wb"))
PY
  chown root:wheel "$LD"; chmod 644 "$LD"
  launchctl bootstrap system "$LD"
  sleep 2; tail -3 /var/log/g14-hotkeys.log
fi
