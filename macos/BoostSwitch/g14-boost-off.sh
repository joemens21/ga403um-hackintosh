#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# CPU boost through AmdCppc: OFF unless /Users/Shared/G14/boost-allow exists (RogControl's "CPU boost" switch creates it).
# A flag older than this boot is removed, so boost is off after every start. /Library/Preferences/g14-boost-allow (the old
# permanent opt-out) is still honoured.
F=/Users/Shared/G14/boost-allow
if [ -e "$F" ]; then
  boot=$(/usr/sbin/sysctl -n kern.boottime | /usr/bin/sed -E 's/.*sec = ([0-9]+).*/\1/')
  [ "$(/usr/bin/stat -f %m "$F")" -lt "$boot" ] && /bin/rm -f "$F"
fi
R=$(/usr/sbin/sysctl -n debug.amdcppc.report 2>/dev/null) || exit 0
if [ -e "$F" ] || [ -e /Library/Preferences/g14-boost-allow ]; then
  echo "$R" | /usr/bin/grep -q 'CpbDis=1' && /usr/sbin/sysctl debug.amdcppc.boost=1 >/dev/null 2>&1
else
  echo "$R" | /usr/bin/grep -q 'CpbDis=0' && /usr/sbin/sysctl debug.amdcppc.boost=0 >/dev/null 2>&1
fi
exit 0
