// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
//
// NVRMAuxBacklight: brightness slider for AUX-only eDP panels (the GA403's OLED, docs/OLED-BRIGHTNESS-DPCD.md) on the
// NullMoth driver 2.5.3, without rebuilding NullMoth.
//
// NullMoth's framebuffer (NVRMFB, NVRMNVDAFramebuffer::publishBacklight) asks its NVRM provider for the panel brightness
// through callPlatformFunction("NVRMBacklight", p1 = NvU32 *percent, p2 != NULL sets it). NVRM (v2.5.3 NVRM.cpp) answers
// with nvkms_xnu_backlight(set, percent), which only works when NVKMS registered a backlight; for an AUX-only panel the GSP
// brightness query fails ("NVKMS INFO: backlight: display 0x200 brightness query status 0x56"), so NVRMFB attaches no
// backlight handler and macOS shows no slider. nvkms_xnu_backlight is exported by com.nullmoth.NVRM; this Lilu plugin
// routes it.
//
// Stage 2 (this build): when NullMoth's own answer fails, the plugin answers itself over the panel's DPCD brightness
// registers (0x721 mode, 0x722/3 11-bit level; docs/OLED-BRIGHTNESS-DPCD.md), through NV0073_CTRL_CMD_DP_AUXCH_CTRL on a
// kernel RM client of its own (NVRM's exported nvRmApiAlloc / nvRmApiControl / nvRmApiFree, the path NVKMS itself uses: it
// wakes the GPU around each call). Percent maps to the level on the same 2.2 gamma curve as G14Hotkeys' 16 steps.
// Switches: boot-arg -nvauxbloff (plugin off), -nvauxblro (answers GET, never writes); sysctl debug.nvauxbl_write=0 (root)
// stops writes at run time. State for anyone, no root needed: sysctl debug.nvauxbl.
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <sys/sysctl.h>

#define MODULE_SHORT "nvauxbl"

using NvBool = unsigned char;
using NvU32 = unsigned int;
using NvHandle = unsigned int;

// ---- RM (open-gpu-kernel-modules 615.78.08 layouts, checked by size) ----------------------------------------------------------
enum : NvU32 {
    NV01_ROOT = 0x0, NV01_DEVICE_0 = 0x80, NV04_DISPLAY_COMMON = 0x73,
    NV0073_CTRL_CMD_DP_AUXCH_CTRL = 0x731341,
    AUX_CMD_TYPE_AUX = 1u << 3, AUX_REQ_WRITE = 0, AUX_REQ_READ = 1,
    PANEL = 0x200,                          // displayId of the eDP OLED (NVKMS: "display 0x200")
    H_DEVICE = 0x4e410080, H_DISP = 0x4e410073,
    LEVEL_MAX = 0x7ff,                      // DPCD 0x724 = 0x0b: 11-bit level
};
struct Nv0000AllocParams { NvHandle hClient; NvU32 processID; char processName[100]; unsigned long long pOsPidInfo; };
struct Nv0080AllocParams {
    NvU32 deviceId; NvHandle hClientShare, hTargetClient, hTargetDevice; NvU32 flags;
    unsigned long long vaSpaceSize __attribute__((aligned(8))), vaStartInternal, vaLimitInternal;
    NvU32 vaMode;
};
struct AuxParams {
    NvU32 subDeviceInstance, displayId; NvBool bAddrOnly; NvU32 cmd, addr; unsigned char data[16]; NvU32 size, replyType, retryTimeMs;
};
static_assert(sizeof(Nv0000AllocParams) == 120, "NV0000_ALLOC_PARAMETERS");
static_assert(sizeof(Nv0080AllocParams) == 56, "NV0080_ALLOC_PARAMETERS");
static_assert(sizeof(AuxParams) == 48, "NV0073_CTRL_DP_AUXCH_CTRL_PARAMS");

using RmAllocFn = NvU32 (*)(NvU32 hClient, NvU32 hParent, NvU32 hObject, NvU32 hClass, void *params);
using RmControlFn = NvU32 (*)(NvU32 hClient, NvU32 hObject, NvU32 cmd, void *params, NvU32 size);
using RmFreeFn = NvU32 (*)(NvU32 hClient, NvU32 hParent, NvU32 hObject);
static RmAllocFn rmAlloc;
static RmControlFn rmControl;
static RmFreeFn rmFree;
static NvHandle hClient;
static IOLock *rmLock;

// percent -> level: max(8, round(2047 * (p / 100) ^ 2.2)), the curve of G14Hotkeys' brightness steps
static const unsigned short levelOf[101] {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 10, 13, 16, 19, 23, 27, 32, 36, 42, 47, 53, 59, 66, 73, 81, 89, 97, 106, 115, 124, 134, 145,
    156, 167, 179, 191, 203, 216, 230, 244, 258, 273, 288, 304, 320, 336, 353, 371, 389, 407, 426, 446, 465, 486, 506, 528,
    549, 572, 594, 618, 641, 665, 690, 715, 741, 767, 793, 821, 848, 876, 905, 934, 964, 994, 1024, 1055, 1087, 1119, 1152,
    1185, 1219, 1253, 1288, 1323, 1359, 1395, 1432, 1469, 1507, 1545, 1584, 1623, 1663, 1704, 1745, 1786, 1829, 1871, 1914,
    1958, 2002, 2047
};
static NvU32 percentOf(NvU32 level)
{
    NvU32 p = 0;
    while (p < 100 && levelOf[p + 1] <= level) p++;
    if (p < 100 && level - levelOf[p] > levelOf[p + 1] - level) p++;
    return p;
}

static mach_vm_address_t orgBacklight {0};
static volatile UInt32 callCount {0};
static int writeEnabled {1};

// what happened so far, for sysctl debug.nvauxbl
static struct {
    bool started, patcher, nvrmSeen, handler, routed, solved;
    int routeErr = -1;
    unsigned lastSet, lastAsked, lastOk, lastPercent;
    unsigned rmStatus, auxStatus, mode, level, gets, sets, fails;
} st;

static int nvauxblSysctl SYSCTL_HANDLER_ARGS
{
    char buf[384];
    snprintf(buf, sizeof(buf), "stage=2 started=%d patcher=%d nvrm_seen=%d handler=%d routed=%d route_err=%d solved=%d "
             "client=%d rm=0x%x aux=0x%x write=%d calls=%u gets=%u sets=%u fails=%u last=%s asked=%u ok=%u percent=%u "
             "mode=%u level=%u", st.started, st.patcher, st.nvrmSeen, st.handler, st.routed, st.routeErr, st.solved,
             hClient != 0, st.rmStatus, st.auxStatus, writeEnabled, callCount, st.gets, st.sets, st.fails,
             callCount ? (st.lastSet ? "SET" : "GET") : "-", st.lastAsked, st.lastOk, st.lastPercent, st.mode, st.level);
    return SYSCTL_OUT(req, buf, strlen(buf) + 1);
}
SYSCTL_PROC(_debug, OID_AUTO, nvauxbl, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, nvauxblSysctl, "A", "");
SYSCTL_INT(_debug, OID_AUTO, nvauxbl_write, CTLFLAG_RW | CTLFLAG_LOCKED, &writeEnabled, 0, "");

// rmLock held. Our own client, device and display objects; on failure everything goes and the next call tries again.
static bool rmOpen()
{
    if (hClient) return true;
    if (!rmAlloc || !rmControl || !rmFree) return false;
    Nv0000AllocParams root {};
    NvU32 s = rmAlloc(0, 0, 0, NV01_ROOT, &root);
    if (s || !root.hClient) { st.rmStatus = s ? s : 0xffff; return false; }
    Nv0080AllocParams dev {};
    dev.deviceId = 0;                                       // the one NVIDIA GPU
    dev.hClientShare = root.hClient;
    if ((s = rmAlloc(root.hClient, root.hClient, H_DEVICE, NV01_DEVICE_0, &dev)) ||
        (s = rmAlloc(root.hClient, H_DEVICE, H_DISP, NV04_DISPLAY_COMMON, nullptr))) {
        rmFree(root.hClient, root.hClient, root.hClient);
        st.rmStatus = s;
        return false;
    }
    st.rmStatus = 0;
    hClient = root.hClient;
    return true;
}

// rmLock held
static bool aux(bool write, NvU32 addr, unsigned char *buf, NvU32 n)
{
    AuxParams p {};
    p.displayId = PANEL; p.addr = addr; p.size = n - 1;
    p.cmd = AUX_CMD_TYPE_AUX | (write ? AUX_REQ_WRITE : AUX_REQ_READ);
    if (write) memcpy(p.data, buf, n);
    NvU32 s = rmControl(hClient, H_DISP, NV0073_CTRL_CMD_DP_AUXCH_CTRL, &p, sizeof(p));
    st.auxStatus = s;
    if (s) return false;
    if (!write) memcpy(buf, p.data, n);
    return true;
}

static bool panelGet(NvU32 *percent)
{
    unsigned char r[3];
    if (!aux(false, 0x721, r, 3)) return false;
    st.mode = r[0]; st.level = (NvU32)r[1] << 8 | r[2];
    *percent = r[0] == 0x02 ? percentOf(st.level) : 100;    // panel in its own mode (after boot / display sleep): full
    return true;
}

static bool panelSet(NvU32 percent)
{
    NvU32 level = levelOf[percent > 100 ? 100 : percent];
    unsigned char b[2] {(unsigned char)(level >> 8), (unsigned char)level}, m = 0x02;
    if (!aux(true, 0x722, b, 2) || !aux(true, 0x721, &m, 1)) return false;
    st.mode = 2; st.level = level;
    return true;
}

// nvkms_xnu_backlight(NvBool set, NvU32 *percent): same contract as NullMoth's (percent 0-100, NV_TRUE on success)
static NvBool wrappedBacklight(NvBool set, NvU32 *percent)
{
    NvU32 asked = percent ? *percent : 0xffffffff;
    NvBool ok = orgBacklight ? FunctionCast(wrappedBacklight, orgBacklight)(set, percent) : 0;
    UInt32 n = OSIncrementAtomic(&callCount);
    if (!ok && percent && rmLock) {                         // NVKMS has no backlight for this panel: answer over DPCD
        if (set) *percent = asked;
        IOLockLock(rmLock);
        if (rmOpen()) {
            if (!set) ok = panelGet(percent), st.gets++;
            else if (writeEnabled) ok = panelSet(asked), st.sets++;
            else ok = 1;                                    // read-only: accept, change nothing
            if (!ok) st.fails++;
        } else {
            st.fails++;
        }
        IOLockUnlock(rmLock);
    }
    st.lastSet = set; st.lastAsked = asked; st.lastOk = ok; st.lastPercent = percent ? *percent : 0xffffffff;
    if (n < 64)                                             // NVRMFB asks once at start, then on every slider / key change
        SYSLOG(MODULE_SHORT, "call #%u %s asked=%u -> ok=%u percent=%u (rm 0x%x aux 0x%x)", n + 1, set ? "SET" : "GET",
               asked, ok, percent ? *percent : 0xffffffff, st.rmStatus, st.auxStatus);
    return ok;
}

static const char *nvrmPaths[] {"/Library/Extensions/NVRM.kext/Contents/MacOS/NVRM"};
static KernelPatcher::KextInfo nvrmKext {
    "com.nullmoth.NVRM", nvrmPaths, arrsize(nvrmPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded
};

static void onNvrmLoaded(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    if (index != nvrmKext.loadIndex) return;
    st.handler = true;
    rmAlloc = patcher.solveSymbol<RmAllocFn>(index, "_nvRmApiAlloc", address, size);
    rmControl = patcher.solveSymbol<RmControlFn>(index, "_nvRmApiControl", address, size);
    rmFree = patcher.solveSymbol<RmFreeFn>(index, "_nvRmApiFree", address, size);
    st.solved = rmAlloc && rmControl && rmFree;
    patcher.clearError();
    KernelPatcher::RouteRequest req("_nvkms_xnu_backlight", wrappedBacklight, orgBacklight);
    if (patcher.routeMultiple(index, &req, 1, address, size)) {
        st.routed = true; st.routeErr = 0;
        SYSLOG(MODULE_SHORT, "routed _nvkms_xnu_backlight in com.nullmoth.NVRM (stage 2, RM api %s)", st.solved ? "ok" : "MISSING");
    } else {
        st.routeErr = (int)patcher.getError();
        SYSLOG(MODULE_SHORT, "FAILED to route _nvkms_xnu_backlight (error %d), NullMoth unchanged", st.routeErr);
    }
    patcher.clearError();
}

// independent of Lilu: note when NVRM's service is published (tells "Lilu missed the load" from "NVRM never came")
static bool onNvrmPublished(void *, void *, IOService *, IONotifier *)
{
    st.nvrmSeen = true;
    return true;
}

static void pluginStart()
{
    st.started = true;
    if (checkKernelArgument("-nvauxblro")) writeEnabled = 0;
    rmLock = IOLockAlloc();
    sysctl_register_oid(&sysctl__debug_nvauxbl);
    sysctl_register_oid(&sysctl__debug_nvauxbl_write);
    SYSLOG(MODULE_SHORT, "start (stage 2, writes %s)", writeEnabled ? "on" : "off");
    lilu.onPatcherLoadForce([](void *, KernelPatcher &) { st.patcher = true; });
    lilu.onKextLoadForce(&nvrmKext, 1, onNvrmLoaded);
    if (OSDictionary *m = IOService::serviceMatching("NVRM")) {
        IOService::addMatchingNotification(gIOPublishNotification, m, onNvrmPublished, nullptr);
        m->release();
    }
}

static const char *bootargOff[] {"-nvauxbloff"};
static const char *bootargDebug[] {"-nvauxbldbg"};
static const char *bootargBeta[] {"-nvauxblbeta"};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Tahoe,
    pluginStart
};
