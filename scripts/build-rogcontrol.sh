#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Builds the RogControl menu bar app (ROG Control Center for macOS) into build/RogControl.app (Xcode command line tools, no Xcode project). Ad-hoc signed.
#   scripts/build-rogcontrol.sh            build
#   scripts/build-rogcontrol.sh --install  build, copy to /Applications, start it at every login (user LaunchAgent), start it now
#                                       (no sudo: /Applications is writable for admins). Turn the login start off in its menu.
set -euo pipefail
cd "$(dirname "$0")/.."
APP=build/RogControl.app
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS"
cp macos/RogControl/Info.plist "$APP/Contents/Info.plist"
# NVIDIA GPU status (macos/Common/nvgpu.c) needs the RM headers of NullMoth's RM version in ~/ogkm615 (see
# scripts/build-g14hotkeys.sh); without them the GPU section shows the driver as unreachable.
O=${OGKM:-$HOME/ogkm615}
mkdir -p build
if [ -d "$O/src/common/sdk/nvidia/inc" ]; then
  clang -arch x86_64 -mmacosx-version-min=13.0 -O1 -Wall -Wno-unknown-pragmas -c macos/Common/nvgpu.c -o build/nvgpu.o \
    -I"$O/src/common/sdk/nvidia/inc" -I"$O/src/common/sdk/nvidia/inc/class" -I"$O/src/common/sdk/nvidia/inc/ctrl" \
    -I"$O/src/nvidia/arch/nvalloc/unix/include" -I"$O/src/common/inc" -I"$O/kernel-open/common/inc" -I"$O/src/nvidia/inc/kernel"
else
  echo "no RM headers in $O: building without NVIDIA GPU status"
  clang -arch x86_64 -mmacosx-version-min=13.0 -O1 -DNVGPU_STUB -c macos/Common/nvgpu.c -o build/nvgpu.o
fi
swiftc -O -target x86_64-apple-macos13.0 -import-objc-header macos/Common/nvgpu.h macos/RogControl/RogControl.swift build/nvgpu.o \
  -o "$APP/Contents/MacOS/RogControl" -framework IOKit -framework SwiftUI
codesign --force --sign - "$APP" >/dev/null
echo "built $APP"
if [ "${1:-}" = "--install" ]; then
  pkill -x RogControl 2>/dev/null || true
  rm -rf /Applications/RogControl.app
  cp -R "$APP" /Applications/
  LA="$HOME/Library/LaunchAgents/com.itlmt7925.RogControl.plist"      # same label and keys the app's "Open at Login" writes
  mkdir -p "$(dirname "$LA")"
  /usr/bin/python3 -I - "$LA" <<'PY'
import plistlib, sys
plistlib.dump({"Label": "com.itlmt7925.RogControl", "ProgramArguments": ["/Applications/RogControl.app/Contents/MacOS/RogControl", "--background"],
               "RunAtLoad": True, "ProcessType": "Interactive", "LimitLoadToSessionType": "Aqua"}, open(sys.argv[1], "wb"))
PY
  plutil -lint "$LA" >/dev/null
  open /Applications/RogControl.app
  echo "installed /Applications/RogControl.app; starts at login ($LA)"
fi
