// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// RM calls as in tools/nvrm-dpcd.c: NVRM user client -> root client / device / NV04_DISPLAY_COMMON -> NV0073_CTRL_CMD_DP_AUXCH_CTRL.
#include <string.h>
#include <IOKit/IOKitLib.h>
#include "nvtypes.h"
#include "nvos.h"
#include "nv_escape.h"
#include "nvstatus.h"
#include "class/cl0080.h"
#include "class/cl0073.h"
#include "ctrl/ctrl0000/ctrl0000gpu.h"
#include "ctrl/ctrl0073/ctrl0073dp.h"
#include "edpbl.h"

enum { SEL_ESC = 1, PANEL = 0x200 };
static io_connect_t conn;
static NvHandle hClient, hDevice, hDisp;

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
static int aux(int write, NvU32 addr, uint8_t *buf, int n)
{
    NV0073_CTRL_DP_AUXCH_CTRL_PARAMS p; memset(&p, 0, sizeof p);
    p.subDeviceInstance = 0; p.displayId = PANEL; p.addr = addr; p.size = n - 1;
    p.cmd = DRF_DEF(0073_CTRL, _DP, _AUXCH_CMD_TYPE, _AUX) |
            (write ? DRF_DEF(0073_CTRL, _DP, _AUXCH_CMD_REQ_TYPE, _WRITE) : DRF_DEF(0073_CTRL, _DP, _AUXCH_CMD_REQ_TYPE, _READ));
    if (write) memcpy(p.data, buf, n);
    if (rmCtl(hDisp, NV0073_CTRL_CMD_DP_AUXCH_CTRL, &p, sizeof p)) return -1;
    if (!write) memcpy(buf, p.data, n);
    return 0;
}

int edpbl_open(void)
{
    if (conn) return 0;
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("NVRM"));
    if (!svc) return -1;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
    IOObjectRelease(svc);
    if (kr) { conn = 0; return -1; }
    hClient = hDevice = hDisp = 0;
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS ids; memset(&ids, 0, sizeof ids);
    NV0000_CTRL_GPU_GET_ID_INFO_V2_PARAMS info; memset(&info, 0, sizeof info);
    NV0080_ALLOC_PARAMETERS a80; memset(&a80, 0, sizeof a80);
    for (int i = 0; i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) ids.gpuIds[i] = NV0000_CTRL_GPU_INVALID_ID;
    if (rmAlloc(0, &hClient, NV01_ROOT_CLIENT, NULL) ||
        rmCtl(hClient, NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS, &ids, sizeof ids) ||
        (info.gpuId = ids.gpuIds[0], rmCtl(hClient, NV0000_CTRL_CMD_GPU_GET_ID_INFO_V2, &info, sizeof info)) ||
        (a80.deviceId = info.deviceInstance, a80.hClientShare = hClient, rmAlloc(hClient, &hDevice, NV01_DEVICE_0, &a80)) ||
        rmAlloc(hDevice, &hDisp, NV04_DISPLAY_COMMON, NULL)) {
        edpbl_close();
        return -1;
    }
    return 0;
}

void edpbl_close(void)
{
    if (conn) IOServiceClose(conn);                  // the RM client goes with the user client
    conn = 0;
}

int edpbl_set(unsigned level)
{
    if (level > EDPBL_MAX) level = EDPBL_MAX;
    uint8_t b[2] = { (uint8_t)(level >> 8), (uint8_t)level }, m = 0x02;
    return aux(1, 0x722, b, 2) || aux(1, 0x721, &m, 1) ? -1 : 0;
}

int edpbl_get(unsigned *level)
{
    uint8_t r[3];
    if (aux(0, 0x721, r, 3)) return -1;
    *level = (unsigned)r[1] << 8 | r[2];
    return r[0] == 0x02;
}
