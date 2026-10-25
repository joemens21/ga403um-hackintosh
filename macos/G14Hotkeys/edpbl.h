// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Built-in OLED brightness over eDP DPCD (AUX-only panel, see docs/OLED-BRIGHTNESS-DPCD.md), from user space through the
// NVRM user client. NullMoth 2.5.3 (RM 615.78.08) allows DP_AUXCH_CTRL only for root clients.
#pragma once
#include <stdint.h>
#include <IOKit/hidsystem/ev_keymap.h>    // NX_KEYTYPE_BRIGHTNESS_*, for the Swift side

#define EDPBL_MAX 0x7FF   // DPCD 0x724 = 0x0b: 11-bit level

int edpbl_open(void);                     // 0 ok; RM objects on display 0x200 (the eDP panel)
void edpbl_close(void);
int edpbl_set(unsigned level);            // 0 ok: 0x722/3 = level, then 0x721 = 2 (DPCD brightness mode)
int edpbl_get(unsigned *level);           // 1 = DPCD mode with *level, 0 = panel in its default mode, -1 = read failed
