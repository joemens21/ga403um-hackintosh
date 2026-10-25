// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// User client ABI of AsusWmi (IOServiceOpen on the "AsusWmi" service, type 0). Used by macos/RogControl.
// Allowed for root, an administrator, or the user logged in at the console (as the itlmt7925 user client).
#ifndef ASUSWMI_ABI_H
#define ASUSWMI_ABI_H

#define ASUSWMI_ABI_VERSION 5

enum {
    kAsusUcGetState  = 0,   // out: struct, uint32_t[ASUS_ST_COUNT]
    kAsusUcSet       = 1,   // in: scalars { what (ASUS_SET_*), value }
    kAsusUcFanRead   = 2,   // in: scalars { fan (0 cpu, 1 gpu), profile (0 balanced, 1 turbo, 2 silent) }; out: struct 16 bytes
                            //     (8 temperatures in °C, then 8 fan duties in %): the firmware's curve for that profile
    kAsusUcFanWrite  = 3,   // in: scalars { fan }, struct 16 bytes (same layout): custom curve, makes the fans "manual"
    kAsusUcFanReset  = 4,   // re-applies the current profile: firmware fan curves, power limits back to the profile's
};

enum {                      // kAsusUcSet "what"
    ASUS_SET_PROFILE = 1,   // 0 balanced (Armoury Crate "Performance"), 1 turbo, 2 silent
    ASUS_SET_SPL     = 2,   // W, PL1 sustained; needs a custom fan curve first (asus-armoury: requires_fan_curve on GA403WM)
    ASUS_SET_SPPT    = 3,   // W, PL2 slow
    ASUS_SET_FPPT    = 4,   // W, PL3 fast
    ASUS_SET_CHARGE  = 5,   // %, battery charge end 20..100
    ASUS_SET_BACKLIGHT = 6, // EC brightness level 0..ASUS_ST_BACKLIGHT_MAX (AWMI.WMAA); not the OLED's: that is NullMoth's AUX path
    ASUS_SET_KBD     = 7,   // keyboard backlight 0..3 (N-KEY HID feature report 0x5A)
    ASUS_SET_SLASH   = 8,   // Slash lighting: ASUS_SLASH_PACK(mode, brightness, interval); brightness 0 = off
    ASUS_SET_AURA    = 9,   // keyboard colour and effect (ABI 4): ASUS_AURA_PACK(mode, speed, 0xRRGGBB)
    ASUS_SET_CPU_TEMP = 10, // ABI 5: CPU temperature target °C 60..100 (AmdCppc, SMU set_tctl_temp)
    ASUS_SET_NV_BOOST = 11, // ABI 5: NVIDIA Dynamic Boost W 5..25 (WMI 0x001200C0)
    ASUS_SET_NV_TEMP  = 12, // ABI 5: NVIDIA temperature target °C 75..87 (WMI 0x001200C2)
};

enum {                      // kAsusUcGetState words
    ASUS_ST_ABI = 0,
    ASUS_ST_FLAGS,          // ASUS_FL_*
    ASUS_ST_PROFILE,        // 0..2 as last set since boot (the BIOS's DSTS does not report it), 0xFFFFFFFF unknown
    ASUS_ST_CHARGE,         // %, as last set since boot (DSTS reads 0 on this BIOS), 0xFFFFFFFF unknown
    ASUS_ST_CPU_FAN_RPM,
    ASUS_ST_GPU_FAN_RPM,
    ASUS_ST_SPL,            // W last set through AsusWmi since boot / the last profile change; 0 = the profile's own
    ASUS_ST_SPPT,
    ASUS_ST_FPPT,
    ASUS_ST_SPL_MIN, ASUS_ST_SPL_MAX,     // ranges for the current power source (asus-armoury, GA403WM)
    ASUS_ST_SPPT_MIN, ASUS_ST_SPPT_MAX,
    ASUS_ST_FPPT_MIN, ASUS_ST_FPPT_MAX,
    ASUS_ST_GPU_MUX,        // raw DSTS
    ASUS_ST_DGPU_DISABLE,   // raw DSTS
    ASUS_ST_NV_BASE_TGP,    // W
    ASUS_ST_BACKLIGHT,      // current level, 0xFFFFFFFF unknown
    ASUS_ST_BACKLIGHT_MAX,  // 0 = no EC backlight control
    ASUS_ST_BAT_DESIGN,     // mAh, design capacity (_BIF); 0 unknown
    ASUS_ST_BAT_FULL,       // mAh, last full charge capacity
    ASUS_ST_BAT_NOW,        // mAh, remaining
    ASUS_ST_BAT_STATE,      // bits 0-7: _BST state (1 discharging, 2 charging), bits 8-31: rate in mA
    ASUS_ST_KBD,            // keyboard backlight 0..3 as last set (the keyboard cannot be read), 0xFFFFFFFF unknown
    ASUS_ST_SLASH,          // Slash setting as last set (ASUS_SLASH_PACK), 0xFFFFFFFF unknown
    ASUS_ST_HID,            // 1: the N-KEY keyboard (0b05:19b6) was found
    ASUS_ST_AURA,           // ABI 4: keyboard colour / effect as last set (ASUS_AURA_PACK), 0xFFFFFFFF unknown
    ASUS_ST_CPU_TEMP,       // ABI 5: CPU temperature target °C as last set since boot, 0xFFFFFFFF not set (firmware's own)
    ASUS_ST_NV_BOOST,       // ABI 5: NVIDIA Dynamic Boost W (last set, else the firmware's DSTS value), 0xFFFFFFFF no device
    ASUS_ST_NV_TEMP,        // ABI 5: NVIDIA temperature target °C (same rule), 0xFFFFFFFF no device
    ASUS_ST_COUNT = 31
};
#define ASUS_ST_COUNT_V3 27 // older clients ask for fewer words (ABI 3: 27, ABI 4: 28); they get what they ask for

#define ASUS_FL_AC         0x1u    // on AC power (ACPI0003 _PSR)
#define ASUS_FL_FAN_CUSTOM 0x2u    // a custom fan curve is active (written since the last profile change)

// Slash: mode index (ASUS_SLASH_MODES order, as G-Helper's SlashMode), brightness 0..3, interval 0..5 (animation speed)
#define ASUS_SLASH_PACK(mode, bright, interval) ((uint32_t)(mode) | ((uint32_t)(bright) << 8) | ((uint32_t)(interval) << 12))
#define ASUS_SLASH_MODE(v)     ((v) & 0xff)
#define ASUS_SLASH_BRIGHT(v)   (((v) >> 8) & 0xf)
#define ASUS_SLASH_INTERVAL(v) (((v) >> 12) & 0xf)
#define ASUS_SLASH_MODES "Static", "Bounce", "Slash", "Loading", "Bit Stream", "Transmission", "Flow", "Flux", \
                         "Phantom", "Spectrum", "Hazard", "Interfacing", "Ramp", "Game Over", "Start", "Buzzer"
#define ASUS_SLASH_MODE_COUNT 16

// Keyboard Aura (one zone): mode index (ASUS_AURA_MODES order), speed 0 slow / 1 normal / 2 fast, colour 0xRRGGBB
// (Rainbow and Colour cycle ignore the colour)
#define ASUS_AURA_PACK(mode, speed, rgb) (((uint32_t)(rgb) & 0xffffffu) | ((uint32_t)(speed) << 24) | ((uint32_t)(mode) << 26))
#define ASUS_AURA_RGB(v)   ((v) & 0xffffff)
#define ASUS_AURA_SPEED(v) (((v) >> 24) & 0x3)
#define ASUS_AURA_MODE(v)  (((v) >> 26) & 0xf)
#define ASUS_AURA_MODES "Static", "Breathe", "Colour cycle", "Rainbow", "Strobe"
#define ASUS_AURA_MODE_COUNT 5

#endif
