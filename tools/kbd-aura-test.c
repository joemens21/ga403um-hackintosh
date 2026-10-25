// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Test: keyboard Aura colour on the N-KEY keyboard (HID 0b05:19b6), feature report 0x5D (127 bytes + ID), with the sequence
// G-Helper sends (Aura.cs): init 5D B9 / 5D "ASUS Tech.Inc." / 5D 05 20 31 00 1A, then 5D B3 00 <mode> R G B <speed> 00 00
// R2 G2 B2, set 5D B5 00 00 00, apply 5D B4. Modes: 0 static, 1 breathe, 2 colour cycle, 3 rainbow, 10 strobe.
// Speeds: 0xE1 slow, 0xEB normal, 0xF5 fast. Build: clang -framework IOKit -framework CoreFoundation kbd-aura-test.c
//   sudo ./kbd-aura-test RRGGBB [mode] [speed]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/hid/IOHIDManager.h>

static IOHIDDeviceRef dev;

static int feat(const uint8_t *cmd, size_t n)
{
    uint8_t b[128] = {0};
    memcpy(b, cmd, n);
    IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, 0x5D, b, sizeof b);
    printf("  %02x %02x ... -> %08x\n", cmd[0], cmd[1], r);
    return r ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strlen(argv[1]) != 6) { fprintf(stderr, "usage: kbd-aura-test RRGGBB [mode] [speed]\n"); return 2; }
    unsigned rgb = (unsigned)strtoul(argv[1], NULL, 16);
    int mode = argc > 2 ? atoi(argv[2]) : 0, speed = argc > 3 ? (int)strtol(argv[3], NULL, 0) : 0xEB;
    IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int vid = 0x0b05, pid = 0x19b6;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    const void *k[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) }, *val[] = { v, p };
    CFDictionaryRef match = CFDictionaryCreate(NULL, k, val, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(m, match);
    IOReturn r = IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
    printf("open manager -> %08x\n", r);
    CFSetRef set = IOHIDManagerCopyDevices(m);
    if (!set || !CFSetGetCount(set)) { fprintf(stderr, "N-KEY keyboard not found\n"); return 1; }
    CFIndex cnt = CFSetGetCount(set);
    const void **devs = calloc((size_t)cnt, sizeof *devs);
    CFSetGetValues(set, devs);
    for (CFIndex i = 0; i < cnt && !dev; i++) {                       // the interface whose descriptor has report 0x5D
        CFNumberRef fl = IOHIDDeviceGetProperty((IOHIDDeviceRef)devs[i], CFSTR(kIOHIDMaxFeatureReportSizeKey));
        int size = 0; if (fl) CFNumberGetValue(fl, kCFNumberIntType, &size);
        printf("interface %ld: max feature report %d\n", (long)i, size);
        if (size >= 128) dev = (IOHIDDeviceRef)devs[i];
    }
    if (!dev) { fprintf(stderr, "no interface with 128-byte feature reports\n"); return 1; }
    uint8_t R = rgb >> 16, G = rgb >> 8, B = rgb;
    const uint8_t i1[] = { 0x5D, 0xB9 }, i3[] = { 0x5D, 0x05, 0x20, 0x31, 0x00, 0x1A };
    uint8_t i2[15] = { 0x5D, 'A', 'S', 'U', 'S', ' ', 'T', 'e', 'c', 'h', '.', 'I', 'n', 'c', '.' };
    const uint8_t aura[13] = { 0x5D, 0xB3, 0x00, (uint8_t)mode, R, G, B, (uint8_t)speed, 0x00, 0x00, R, G, B };
    const uint8_t setm[] = { 0x5D, 0xB5, 0x00, 0x00, 0x00 }, apply[] = { 0x5D, 0xB4 };
    printf("colour %06x mode %d speed %02x\n", rgb, mode, speed);
    if (feat(i1, sizeof i1) || feat(i2, sizeof i2) || feat(i3, sizeof i3) || feat(aura, sizeof aura) ||
        feat(setm, sizeof setm) || feat(apply, sizeof apply)) return 1;
    puts("OK: sent");
    return 0;
}
