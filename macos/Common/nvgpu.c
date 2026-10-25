// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// RM calls as in macos/G14Hotkeys/edpbl.c: NVRM user client -> root client / device / subdevice -> NV2080 controls.
// Built with -DNVGPU_STUB when the RM headers (~/ogkm615) are not there: every call then reports the driver unreachable.
#include <string.h>
#include "nvgpu.h"

#ifdef NVGPU_STUB
int nvgpu_open(void) { return -1; }
int nvgpu_read(nvgpu_stat *s) { (void)s; return -1; }
void nvgpu_close(void) {}
#else
#include <stdlib.h>
#include <IOKit/IOKitLib.h>
#include "nvtypes.h"
#include "nvos.h"
#include "nv_escape.h"
#include "nvstatus.h"
#include "class/cl0080.h"
#include "class/cl2080.h"
#include "ctrl/ctrl0000/ctrl0000gpu.h"
#include "ctrl/ctrl2080/ctrl2080perf.h"
#include "ctrl/ctrl2080/ctrl2080thermal.h"
#include "ctrl/ctrl2080/ctrl2080clk.h"
#include "ctrl/ctrl2080/ctrl2080fb.h"

enum { SEL_ESC = 1 };
static io_connect_t conn;
static NvHandle hClient, hDevice, hSub;
static NV2080_CTRL_PMUMON_GET_SAMPLES_SUPER clkPos;      // clock queue position, so each read averages only new samples

static int esc(uint32_t cmd, void *data, size_t size)
{
    uint64_t in = cmd; size_t out = size;
    return IOConnectCallMethod(conn, SEL_ESC, &in, 1, data, size, NULL, NULL, data, &out) ? -1 : 0;
}
static NvU32 rmAlloc(NvHandle parent, NvHandle *h, NvU32 cls, void *params)
{
    NVOS21_PARAMETERS p; memset(&p, 0, sizeof p);
    p.hRoot = hClient; p.hObjectParent = parent; p.hObjectNew = *h; p.hClass = cls; p.pAllocParms = (NvP64)(uintptr_t)params;
    if (esc(NV_ESC_RM_ALLOC, &p, sizeof p)) return 0xFFFF;
    *h = p.hObjectNew; return p.status;
}
static NvU32 rmCtl(NvHandle obj, NvU32 cmd, void *params, NvU32 size)
{
    NVOS54_PARAMETERS p; memset(&p, 0, sizeof p);
    p.hClient = hClient; p.hObject = obj; p.cmd = cmd; p.params = (NvP64)(uintptr_t)params; p.paramsSize = size;
    if (esc(NV_ESC_RM_CONTROL, &p, sizeof p)) return 0xFFFF;
    return p.status;
}

int nvgpu_open(void)
{
    if (conn) return 0;
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("NVRM"));
    if (!svc) return -1;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
    IOObjectRelease(svc);
    if (kr) { conn = 0; return -1; }
    hClient = hDevice = hSub = 0;
    memset(&clkPos, 0, sizeof clkPos);
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS ids; memset(&ids, 0, sizeof ids);
    NV0000_CTRL_GPU_GET_ID_INFO_V2_PARAMS info; memset(&info, 0, sizeof info);
    NV0080_ALLOC_PARAMETERS a80; memset(&a80, 0, sizeof a80);
    NV2080_ALLOC_PARAMETERS a20; memset(&a20, 0, sizeof a20);
    for (int i = 0; i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) ids.gpuIds[i] = NV0000_CTRL_GPU_INVALID_ID;
    if (rmAlloc(0, &hClient, NV01_ROOT_CLIENT, NULL) ||
        rmCtl(hClient, NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS, &ids, sizeof ids) ||
        (info.gpuId = ids.gpuIds[0], rmCtl(hClient, NV0000_CTRL_CMD_GPU_GET_ID_INFO_V2, &info, sizeof info)) ||
        (a80.deviceId = info.deviceInstance, a80.hClientShare = hClient, rmAlloc(hClient, &hDevice, NV01_DEVICE_0, &a80)) ||
        rmAlloc(hDevice, &hSub, NV20_SUBDEVICE_0, &a20)) {
        nvgpu_close();
        return -1;
    }
    return 0;
}

void nvgpu_close(void)
{
    if (conn) IOServiceClose(conn);                  // the RM client goes with the user client
    conn = 0;
}

int nvgpu_read(nvgpu_stat *st)
{
    st->tempC = st->pstate = st->gfxPct = st->memPct = st->encPct = st->decPct = NVGPU_NONE;
    st->coreMHz = st->memMHz = st->vramUsedMB = st->vramTotalMB = NVGPU_NONE;
    if (nvgpu_open()) return -1;

    NV2080_CTRL_THERMAL_SYSTEM_EXECUTE_V2_PARAMS *th = calloc(1, sizeof *th);
    if (!th) return -1;
    th->clientAPIVersion = THERMAL_SYSTEM_API_VER; th->clientAPIRevision = THERMAL_SYSTEM_API_REV;
    th->clientInstructionSizeOf = sizeof(NV2080_CTRL_THERMAL_SYSTEM_INSTRUCTION);
    th->instructionListSize = 1;
    th->instructionList[0].opcode = NV2080_CTRL_THERMAL_SYSTEM_GET_STATUS_SENSOR_READING_OPCODE;
    th->instructionList[0].operands.getStatusSensorReading.sensorIndex = 0;
    st->status[0] = rmCtl(hSub, NV2080_CTRL_CMD_THERMAL_SYSTEM_EXECUTE_V2, th, sizeof *th);
    if (!st->status[0] && th->successfulInstructions == 1) st->tempC = th->instructionList[0].operands.getStatusSensorReading.value;
    free(th);
    if (st->status[0] == 0xFFFF) { nvgpu_close(); return -1; }   // the user client went away (driver restart): reopen next time

    NV2080_CTRL_PERF_GET_CURRENT_PSTATE_PARAMS ps; memset(&ps, 0, sizeof ps);
    st->status[1] = rmCtl(hSub, NV2080_CTRL_CMD_PERF_GET_CURRENT_PSTATE, &ps, sizeof ps);
    if (!st->status[1])
        for (int i = 0; i < 16; i++) if (ps.currPstate == (1u << i)) st->pstate = i;    // NV2080_CTRL_PERF_PSTATES_P0 = bit 0

    static NV2080_CTRL_PERF_GET_GPUMON_PERFMON_UTIL_SAMPLES_V2_PARAMS ut;
    memset(&ut, 0, sizeof ut);
    ut.type = NV2080_CTRL_GPUMON_SAMPLE_TYPE_PERFMON_UTIL; ut.bufSize = sizeof ut.samples;
    st->status[2] = rmCtl(hSub, NV2080_CTRL_CMD_PERF_GET_GPUMON_PERFMON_UTIL_SAMPLES_V2, &ut, sizeof ut);
    if (!st->status[2]) {
        const NV2080_CTRL_PERF_GPUMON_PERFMON_UTIL_SAMPLE *last = NULL;
        for (int i = 0; i < NV2080_CTRL_PERF_GPUMON_SAMPLE_COUNT_PERFMON_UTIL; i++)
            if (ut.samples[i].base.timeStamp && (!last || ut.samples[i].base.timeStamp > last->base.timeStamp))
                last = &ut.samples[i];
        if (last) {                                                       // util is in 1/100 % (10000 = busy)
            st->gfxPct = (int)(last->gr.util / 100); st->memPct = (int)(last->fb.util / 100);
            st->encPct = (int)(last->nvenc.util / 100); st->decPct = (int)(last->nvdec.util / 100);
        }
    }

    // activeGpcClkFreqKHz stays at the boost ceiling on this GPU; gpcClkFreqKHz is the clock actually programmed
    static NV2080_CTRL_CLK_PMUMON_CLK_DOMAINS_GET_SAMPLES_PARAMS ck;
    memset(&ck, 0, sizeof ck);
    ck.super = clkPos;
    st->status[3] = rmCtl(hSub, NV2080_CTRL_CMD_CLK_PMUMON_CLK_DOMAINS_GET_SAMPLES, &ck, sizeof ck);
    if (!st->status[3]) {
        clkPos = ck.super;
        unsigned long long gpc = 0, dram = 0; unsigned ng = 0, nd = 0;
        for (NvU32 i = 0; i < ck.super.numSamples && i < NV2080_CTRL_CLK_PMUMON_CLK_DOMAINS_SAMPLE_COUNT; i++) {
            const NV2080_CTRL_CLK_PMUMON_CLK_DOMAINS_SAMPLE *c = &ck.samples[i];
            if (c->gpcClkFreqKHz != NV2080_CTRL_CLK_PMUMON_CLK_DOMAINS_SAMPLE_INVALID) gpc += c->gpcClkFreqKHz, ng++;
            if (c->dramClkFreqKHz != NV2080_CTRL_CLK_PMUMON_CLK_DOMAINS_SAMPLE_INVALID) dram += c->dramClkFreqKHz, nd++;
        }
        if (ng) st->coreMHz = (int)(gpc / ng / 1000);
        if (nd) st->memMHz = (int)(dram / nd / 1000);
    }

    NV2080_CTRL_FB_GET_INFO_V2_PARAMS fb; memset(&fb, 0, sizeof fb);
    fb.fbInfoListSize = 2;
    fb.fbInfoList[0].index = NV2080_CTRL_FB_INFO_INDEX_TOTAL_RAM_SIZE;    // kilobytes
    fb.fbInfoList[1].index = NV2080_CTRL_FB_INFO_INDEX_HEAP_FREE;
    st->status[4] = rmCtl(hSub, NV2080_CTRL_CMD_FB_GET_INFO_V2, &fb, sizeof fb);
    if (!st->status[4]) {
        st->vramTotalMB = (int)(fb.fbInfoList[0].data / 1024);
        st->vramUsedMB = (int)((fb.fbInfoList[0].data - fb.fbInfoList[1].data) / 1024);
    }
    return 0;
}
#endif
