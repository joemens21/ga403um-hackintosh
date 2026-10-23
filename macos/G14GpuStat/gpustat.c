// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
//
// g14-gpustat: NVIDIA GPU status on the NullMoth driver, where there is no nvidia-smi (queries: macos/Common/nvgpu.c).
//   g14-gpustat          one line: temperature, P-state, engine load, clocks, video memory
//   g14-gpustat -w [s]   a line every s seconds (default 1) until Ctrl-C
//   g14-gpustat -v       also each query's raw RM status (0 = answered)
// No root needed (NullMoth 2.5.3). Power draw has no control in the open RM interface, so it is not shown.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nvgpu.h"

// a value, or "-" for a field the GPU did not answer (rotating buffers: several per printf)
static const char *v(int x)
{
    static char buf[12][16]; static int i;
    char *b = buf[i++ % 12];
    if (x == NVGPU_NONE) return "-";
    snprintf(b, sizeof buf[0], "%d", x);
    return b;
}

int main(int argc, char **argv)
{
    int watch = 0, verbose = 0; double every = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-w")) { watch = 1; if (i + 1 < argc && atof(argv[i + 1]) > 0) every = atof(argv[++i]); }
        else { fprintf(stderr, "usage: g14-gpustat [-w [seconds]] [-v]\n"); return 2; }
    }
    do {
        nvgpu_stat s;
        if (nvgpu_read(&s)) { fprintf(stderr, "g14-gpustat: cannot reach the NVIDIA driver (NVRM)\n"); return 1; }
        printf("temp %s C  P%s  gfx %s%%  mem %s%%  enc %s%%  dec %s%%  core %s MHz  mem %s MHz  vram %s/%s MB\n",
               v(s.tempC), v(s.pstate), v(s.gfxPct), v(s.memPct), v(s.encPct), v(s.decPct), v(s.coreMHz), v(s.memMHz),
               v(s.vramUsedMB), v(s.vramTotalMB));
        if (verbose) printf("  rm status thermal 0x%x pstate 0x%x util 0x%x clocks 0x%x fb 0x%x\n",
                            s.status[0], s.status[1], s.status[2], s.status[3], s.status[4]);
        fflush(stdout);
        if (watch) usleep((useconds_t)(every * 1e6));
    } while (watch);
    nvgpu_close();
    return 0;
}
