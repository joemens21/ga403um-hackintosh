#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# CPU boost switch (needs AmdCppc with the boost switch for family 0x19): boost stays OFF by default and after every start;
# RogControl's "CPU boost" switch creates /Users/Shared/G14/boost-allow and the root LaunchDaemon com.g14.boostoff turns
# boost on while it exists. launchd runs macos/BoostSwitch/g14-boost-off.sh when that folder changes and every 60 s.
#   sudo bash scripts/macos-boost-switch-install.sh [user]     user = owner of the flag folder (default: the console user)
#   sudo bash scripts/macos-boost-switch-install.sh --undo
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
LABEL=com.g14.boostoff
SH=/usr/local/bin/g14-boost-off.sh
LD=/Library/LaunchDaemons/$LABEL.plist
if [ "${1:-}" = --undo ]; then
  launchctl bootout "system/$LABEL" 2>/dev/null || true
  rm -f "$LD" "$SH"
  echo "boost switch removed (boost stays as it is until the next start)"; exit 0
fi
OWNER="${1:-$(stat -f %Su /dev/console)}"
mkdir -p /Users/Shared/G14 /usr/local/bin
chown "$OWNER":staff /Users/Shared/G14; chmod 755 /Users/Shared/G14
install -o root -g wheel -m 755 macos/BoostSwitch/g14-boost-off.sh "$SH"
launchctl bootout "system/$LABEL" 2>/dev/null || true
/usr/bin/python3 -I - "$LD" <<'PY'
import plistlib, sys
plistlib.dump({"Label": "com.g14.boostoff", "ProgramArguments": ["/usr/local/bin/g14-boost-off.sh"], "RunAtLoad": True,
               "StartInterval": 60, "WatchPaths": ["/Users/Shared/G14"]}, open(sys.argv[1], "wb"))
PY
chown root:wheel "$LD"; chmod 644 "$LD"
plutil -lint "$LD"
launchctl bootstrap system "$LD"
echo "installed $SH and $LD; flag folder /Users/Shared/G14 (owner $OWNER)"
