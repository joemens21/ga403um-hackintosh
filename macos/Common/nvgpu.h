// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// NVIDIA GPU status on the NullMoth driver (no nvidia-smi on macOS), through the NVRM user client: temperature, P-state,
// engine utilisation, clocks, video memory. No root needed (NullMoth 2.5.3). Power draw has no control in the open RM
// interface, so there is none here. Used by g14-gpustat and RogControl.
#pragma once
#include <stdint.h>

#define NVGPU_NONE (-1)                   // a field the GPU did not answer

typedef struct {
    int tempC;                            // GPU core sensor
    int pstate;                           // 0 (P0, full) .. 15
    int gfxPct, memPct, encPct, decPct;   // engine load of the newest sample, percent
    int coreMHz, memMHz;                  // averaged over the PMU samples (every 20 ms) since the previous read
    int vramUsedMB, vramTotalMB;
    uint32_t status[5];                   // raw RM status per query: thermal, pstate, util, clocks, fb (0 = answered)
} nvgpu_stat;

int nvgpu_open(void);                     // 0 ok; keeps one RM client until nvgpu_close
int nvgpu_read(nvgpu_stat *s);            // 0 ok (fields that failed are NVGPU_NONE), -1 the driver is not reachable
void nvgpu_close(void);
