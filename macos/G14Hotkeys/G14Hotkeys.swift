// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
//
// G14Hotkeys: Fn keys that the GA403 sends as input reports on the N-KEY keyboard (HID 0b05:19b6, usage page 0xFF89)
// instead of WMI events, so AsusWmi's hotkey handler never sees them. Runs as a root LaunchDaemon (the AsusWmi sysctls
// need root). Codes seen on a GA403UM:
//   report 0x5A 0xC5 / 0xC4   Fn+F2 / Fn+F3   keyboard light down / up    -> sysctl debug.asuswmi.kbd (0..3)
//   report 0x5A 0xB3          Fn+F4           AURA: Slash on / off        -> sysctl debug.asuswmi.slash (brightness 0 <-> 2)
//   report 0x5A 0x7C          Fn+F9           mic mute                    -> default input: mute, or volume 0 / restore
//   report 0x5A 0x10 / 0x20   Fn+F7 / Fn+F8   OLED brightness down / up   -> system brightness keys, else DPCD (edpbl.c)
//   report 0x02 0xE2          Fn+F1           speaker mute (consumer)     -> default output: mute, or volume 0 / restore
//   report 0x5A 0x38          ROG key         opens RogControl (its window) in the console user's session
// 0x5A 0x00 is the key release. Built-in Output (AppleALC) has no mute control, hence the volume fallback.
import Foundation
import IOKit.hid
import CoreAudio
import AudioToolbox
import AppKit

func log(_ s: String) {
    let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HH:mm:ss"
    print("\(f.string(from: Date())) \(s)"); fflush(stdout)
}

// MARK: AsusWmi sysctls

func sysctlGet(_ name: String) -> Int32? {
    var v: Int32 = 0, n = MemoryLayout<Int32>.size
    return sysctlbyname(name, &v, &n, nil, 0) == 0 ? v : nil
}

func sysctlSet(_ name: String, _ value: Int32) {
    var v = value
    let r = sysctlbyname(name, nil, nil, &v, MemoryLayout<Int32>.size)
    log("\(name)=\(value) -> \(r == 0 ? "ok" : String(cString: strerror(errno)))")
}

func kbdStep(_ up: Bool) {
    let lvl = sysctlGet("debug.asuswmi.kbd") ?? -1                       // -1: not set since boot
    sysctlSet("debug.asuswmi.kbd", up ? (lvl < 0 ? 1 : min(lvl + 1, 3)) : (lvl < 0 ? 0 : max(lvl - 1, 0)))
}

func slashToggle() {
    var s = sysctlGet("debug.asuswmi.slash") ?? -1
    if s < 0 { s = 2 }                                                   // unknown: mode Slash, off
    let bright = (s >> 8) & 0xf
    sysctlSet("debug.asuswmi.slash", (s & ~0xf00) | ((bright != 0 ? 0 : 2) << 8))
}

// MARK: CoreAudio mute

func defaultDevice(input: Bool) -> AudioObjectID {
    var addr = AudioObjectPropertyAddress(
        mSelector: input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
        mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var dev = AudioObjectID(kAudioObjectUnknown), n = UInt32(MemoryLayout<AudioObjectID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &n, &dev)
    return dev
}

func settable(_ dev: AudioObjectID, _ addr: inout AudioObjectPropertyAddress) -> Bool {
    var ok: DarwinBoolean = false
    return AudioObjectHasProperty(dev, &addr) && AudioObjectIsPropertySettable(dev, &addr, &ok) == noErr && ok.boolValue
}

var savedVolume: [Bool: Float32] = [:]

func toggleMute(input: Bool) {
    let dev = defaultDevice(input: input), what = input ? "mic" : "speaker"
    let scope = input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput
    guard dev != kAudioObjectUnknown else { log("\(what): no default device"); return }
    var mute = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyMute, mScope: scope,
                                          mElement: kAudioObjectPropertyElementMain)
    if settable(dev, &mute) {
        var v: UInt32 = 0, n = UInt32(MemoryLayout<UInt32>.size)
        AudioObjectGetPropertyData(dev, &mute, 0, nil, &n, &v)
        v = v == 0 ? 1 : 0
        let r = AudioObjectSetPropertyData(dev, &mute, 0, nil, n, &v)
        log("\(what) mute=\(v) -> \(r)")
        return
    }
    var vol = AudioObjectPropertyAddress(mSelector: kAudioHardwareServiceDeviceProperty_VirtualMainVolume, mScope: scope,
                                         mElement: kAudioObjectPropertyElementMain)
    guard settable(dev, &vol) else { log("\(what): no mute or volume control"); return }
    var v: Float32 = 0, n = UInt32(MemoryLayout<Float32>.size)
    AudioObjectGetPropertyData(dev, &vol, 0, nil, &n, &v)
    if v > 0.001 { savedVolume[input] = v; v = 0 } else { v = savedVolume[input] ?? 0.5 }
    let r = AudioObjectSetPropertyData(dev, &vol, 0, nil, n, &v)
    log("\(what) volume=\(v) -> \(r)")
}

// MARK: OLED brightness

// With the NVRMAuxBacklight plugin (sysctl debug.nvauxbl) macOS owns the brightness: Fn+F7/F8 post the system brightness keys,
// so the slider, the on-screen indicator and the panel move together. If macOS does not act on the key within
// 0.6 s (no new plugin write), or the plugin is not there, the key steps the panel directly over DPCD (edpbl.c), 16 steps on a
// 2.2 gamma curve over the 11-bit level. The panel falls back to its default mode (0x721 = 1) at boot and may after display
// sleep; the 5 s check puts the last level back only then, and otherwise just remembers the level (kept in root's defaults).
var blLast = UserDefaults.standard.object(forKey: "brightnessLevel") as? UInt32 ?? UInt32(EDPBL_MAX)

func blLevel(_ step: Int) -> UInt32 { max(8, UInt32((Double(EDPBL_MAX) * pow(Double(step) / 16, 2.2)).rounded())) }

func blRemember(_ lvl: UInt32) {
    if lvl == blLast { return }
    blLast = lvl
    UserDefaults.standard.set(lvl, forKey: "brightnessLevel")
}

@discardableResult
func blApply(_ lvl: UInt32) -> Bool {
    if edpbl_open() != 0 { return false }
    if edpbl_set(lvl) != 0 { edpbl_close(); return false }
    blRemember(lvl)
    return true
}

// the plugin's write counter, nil without the plugin
func pluginSets() -> Int? {
    var buf = [CChar](repeating: 0, count: 512), n = buf.count
    guard sysctlbyname("debug.nvauxbl", &buf, &n, nil, 0) == 0,
          let r = String(cString: buf).range(of: #"sets=(\d+)"#, options: .regularExpression) else { return nil }
    return Int(String(cString: buf)[r].dropFirst(5))
}

func brightnessDirect(_ up: Bool) {
    var cur: UInt32 = 0
    if edpbl_open() != 0 { log("brightness: panel not reachable"); return }
    if edpbl_get(&cur) != 1 { cur = blLast }
    let step = (0...16).min { abs(Int(blLevel($0)) - Int(cur)) < abs(Int(blLevel($1)) - Int(cur)) } ?? 16
    let lvl = blLevel(max(1, min(16, step + (up ? 1 : -1))))
    log("brightness direct level \(cur) -> \(lvl) -> \(blApply(lvl) ? "ok" : "failed")")
}

// the system brightness key (NX_KEYTYPE_BRIGHTNESS_UP / _DOWN) as a system-defined event through the window server's HID
// tap; needs Accessibility. (IOHIDPostEvent is refused for this daemon with kIOReturnNotPrivileged even with Accessibility.)
func cgBrightnessKey(_ up: Bool) {
    let key = up ? NX_KEYTYPE_BRIGHTNESS_UP : NX_KEYTYPE_BRIGHTNESS_DOWN
    for down in [true, false] {
        let ev = NSEvent.otherEvent(with: .systemDefined, location: .zero,
                                    modifierFlags: NSEvent.ModifierFlags(rawValue: down ? 0xa00 : 0xb00), timestamp: 0,
                                    windowNumber: 0, context: nil, subtype: 8,
                                    data1: Int(key) << 16 | (down ? 0xa : 0xb) << 8, data2: -1)
        ev?.cgEvent?.post(tap: .cghidEventTap)
    }
}

func brightnessKey(_ up: Bool) {
    guard let before = pluginSets() else { brightnessDirect(up); return }
    cgBrightnessKey(up)
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
        if let after = pluginSets(), after > before { return }
        // already at the end the key points to: macOS had nothing to change
        var cur: UInt32 = 0
        if edpbl_open() == 0, edpbl_get(&cur) == 1, up ? cur >= UInt32(EDPBL_MAX) : cur <= blLevel(1) { return }
        log("brightness key not taken by macOS")
        brightnessDirect(up)
    }
}

func blKeep() {
    if edpbl_open() != 0 { return }                                      // NVRM not up yet
    var lvl: UInt32 = 0
    let mode = edpbl_get(&lvl)
    if mode < 0 { edpbl_close(); return }                                // display asleep, or the RM client went stale
    if mode == 1 { blRemember(lvl); return }
    log("panel in its own mode: level \(blLast) restored -> \(blApply(blLast) ? "ok" : "failed")")
}

// MARK: ROG key

// This daemon runs as root outside the login session: `open` has to run in the console user's session (launchctl asuser).
// A running RogControl gets a reopen event and shows its window; otherwise it starts with the window open.
func openRogControl() {
    var st = stat()
    guard stat("/dev/console", &st) == 0, st.st_uid != 0 else { log("ROG key: nobody logged in"); return }
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/launchctl")
    p.arguments = ["asuser", String(st.st_uid), "/usr/bin/open", "-b", "com.itlmt7925.RogControl"]
    do { try p.run(); log("ROG key: RogControl") } catch { log("ROG key: open failed: \(error)") }
}

// MARK: N-KEY input reports

// A press can arrive twice (same code, before or after its 0x00 release), so a code acts once until it is released and
// not again within 250 ms.
var held: [UInt32: UInt8] = [:], lastAct: [UInt32: (UInt8, Date)] = [:]

func pressed(_ id: UInt32, _ code: UInt8) -> Bool {
    if code == 0 { held[id] = 0; return false }
    if held[id] == code { return false }
    held[id] = code
    if let (c, t) = lastAct[id], c == code, Date().timeIntervalSince(t) < 0.25 { return false }
    lastAct[id] = (code, Date())
    return true
}

func handle(_ id: UInt32, _ r: UnsafeMutablePointer<UInt8>, _ len: Int) {
    if (id == 0x5A || id == 0x02) && len >= 2 && !pressed(id, r[1]) { return }
    if id == 0x5A && len >= 2 {
        switch r[1] {
        case 0xC4: kbdStep(true)
        case 0xC5: kbdStep(false)
        case 0xB3: slashToggle()
        case 0x7C: toggleMute(input: true)
        case 0x10: brightnessKey(false)
        case 0x20: brightnessKey(true)
        case 0x38: openRogControl()
        default: log(String(format: "key 5a %02x ignored", r[1]))
        }
    } else if id == 0x02 && len >= 3 && r[1] == 0xE2 && r[2] == 0x00 {
        toggleMute(input: false)
    }
}

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(mgr, [kIOHIDVendorIDKey: 0x0b05, kIOHIDProductIDKey: 0x19b6] as CFDictionary)
IOHIDManagerRegisterInputReportCallback(mgr, { _, _, _, _, id, r, len in handle(id, r, len) }, nil)
IOHIDManagerRegisterDeviceMatchingCallback(mgr, { _, _, _, _ in log("N-KEY keyboard attached") }, nil)
IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
let res = IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
log(String(format: "G14Hotkeys started, open %08x", res))
if res != kIOReturnSuccess { exit(1) }                                    // launchd restarts it (ThrottleInterval)
blKeep()
Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in blKeep() }
CFRunLoopRun()
