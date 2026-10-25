# GA403UM setup (ROG Zephyrus G14 2025, RTX 5060)

How this repository's `ga403um` branch becomes a working macOS install on the **GA403UM** (Ryzen 9 270 "Hawk Point", Zen 4,
family 0x19 model 0x75; RTX 5060; 2880 x 1800 OLED). The rest of the repository targets the GA403WM (Strix Point); this page
lists what differs and everything needed to rebuild this machine from scratch.

Reference state (October 2026): macOS 15.8.1 (24H32), OpenCore 1.0.9 (Zen 5 patched DEBUG build of OpenCorePkg 963025f),
NullMoth NVIDIA driver 2.5.3 (RM 615.78.08).

## 1. What works

| Area | State | Pieces |
|---|---|---|
| Wi-Fi (MT7925) | WPA2-PSK, open, WPA2-Enterprise (eduroam, EAP-TTLS/PAP), 2.4 GHz ch 1-11 and 5 GHz; access point chosen by signal | `itlmt7925`, `itlmt7925Native` (`docs/ENTERPRISE-WIFI.md`) |
| Bluetooth | classic and BLE | `MtkBtFw`, `BtLeDlFix`, `BlueToolFixup` |
| Speakers (2 x CS35L56 + woofer) | full range, bass at boot | `StrixAmp`, `strixampctl` daemon at -6 dB, AppleALC layout 33 (`docs/AUDIO-ROG.md`) |
| OLED brightness | macOS slider and Fn+F7 / F8 in step | `NVRMAuxBacklight` (Lilu plugin), G14Hotkeys |
| Fn keys | keyboard light, Slash, mic / speaker mute, brightness; the ROG key opens RogControl | G14Hotkeys (N-KEY HID), AsusWmi |
| ROG controls | profiles, CPU power limits and temperature target (SMU), fan curves (editable), charge limit, keyboard colour / effects, Slash, CPU boost switch, GPU status, history; NVIDIA Dynamic Boost / temperature target (accepted by the BIOS, effect not measured) | RogControl, AsusWmi ABI 5, AmdCppc |
| CPU | boost off by default (max 4.0 GHz), switchable; clocks, Tctl, package power | AmdCppc (`feat/amdcppc-zen4-boost`), boost switch daemon |
| Webcam | 640 x 480 MJPEG 30 fps only (15 fps and often 720p / 1080p set the UVC frame error bit; macOS defaults to 1080p 15 fps, so apps show black): OBS Video Capture Device at 640 x 480 / 30 + virtual camera | OBS (started from Terminal, section 7) |
| Battery | charge limit (80 % raw = 85 % in the UI), watts, time left, cycles | AsusWmi, SMCBatteryManager |
| SD reader (Realtek RTS525A) | SDHC / SDXC cards mount at boot and when inserted later (tested: 256 GB SDXC, exFAT; eject and re-insert works) | `RealtekCardReader` 0.9.8: FireWolf's 0.9.7 built from source with a power-up delay (upstream left it at 0 ms, so the card missed CMD8 and stayed busy) and CMD8 retries; boot-args `iosdpwrd=<ms>` (default 20), `iosdc8na=<n>` (default 3) |

Not working or parked: HDMI hot-plug (connect HDMI before power-on), sleep / wake (sleep is turned off), AirDrop, WPA3-SAE,
802.1X re-authentication on long sessions, Bluetooth LE scanning after an MT7925 Bluetooth firmware crash (phone passkeys
hang until a restart; never switch Bluetooth off and on: the chip only comes back after a restart).

## 2. Branch

`ga403um` merges, with no conflicts: `wifi/gaps-24h32`, `feat/amdcppc-zen4-boost`, `feat/g14-hotkeys`,
`feat/nvrm-aux-backlight`, `feat/g14-gpustat`. The upstream audio research branch (`audio/strixamp-ga403um`) is **not**
merged (it rewrites RogControl); its StrixAmp, `strixampctl`, CS35L56 firmware and AppleALC patch were brought over unchanged.
Built from this branch, every kext, app and tool matches what runs on the reference machine (same UUIDs, or only Lilu's build
date stamp different).

## 3. Build prerequisites

- Xcode command line tools (`xcode-select --install`).
- RM headers of NullMoth's RM version, for G14Hotkeys, g14-gpustat and RogControl's GPU section:
  ```bash
  git clone --depth 1 --branch 615.78.08 --filter=blob:none --sparse https://github.com/NVIDIA/open-gpu-kernel-modules ~/ogkm615
  ```
  ```bash
  git -C ~/ogkm615 sparse-checkout set src/common/sdk/nvidia/inc src/common/inc src/nvidia/arch/nvalloc/unix/include kernel-open/common/inc src/nvidia/inc/kernel
  ```
- Lilu headers: `scripts/build.sh` clones acidanthera/Lilu into `build/Lilu` (or set `LILU=<path>`).
- AppleALC reference sources at commit **ff01db2** (AppleALC 1.9.9), which `scripts/build-applealc-ga403wm.sh` expects in
  `build/AppleALC-reference` and does not fetch itself:
  ```bash
  git clone https://github.com/acidanthera/AppleALC build/AppleALC-reference && git -C build/AppleALC-reference checkout ff01db2
  ```

## 4. Build

| Piece | Command | Output |
|---|---|---|
| all repo kexts | `bash scripts/build.sh` (or name them: `bash scripts/build.sh AsusWmi AmdCppc`) | `build/*.kext` |
| AppleALC (layout 33) | `bash scripts/build-applealc-ga403wm.sh` | `build/AppleALC-GA403WM.kext` (goes on the EFI as `AppleALC.kext`) |
| RogControl | `bash scripts/build-rogcontrol.sh --install` (no sudo) | `/Applications/RogControl.app`, login item |
| G14Hotkeys | `bash scripts/build-g14hotkeys.sh`, then `sudo bash scripts/build-g14hotkeys.sh --install` | `/usr/local/bin/g14-hotkeys`, LaunchDaemon |
| strixampctl | `bash scripts/build-strixampctl.sh`, then `sudo bash scripts/macos-strixamp-install.sh --vol -6` | amp daemon, firmware in `/usr/local/share/strixamp/fw` |
| CPU boost switch | `sudo bash scripts/macos-boost-switch-install.sh` | `/usr/local/bin/g14-boost-off.sh`, LaunchDaemon `com.g14.boostoff` |
| g14-gpustat | `bash scripts/build-g14gpustat.sh` (`sudo ... --install` copies it to `/usr/local/bin`) | `build/g14-gpustat` |
| TapGuard | `bash scripts/build-tapguard.sh --install` | touchpad guard while typing (LaunchAgent) |

Scripts are not executable in the repository: always run them through `bash`.

## 5. EFI

Kexts in `EFI/OC/Kexts` (order in `Kernel/Add` as in `efi/OC/config.plist`; VoodooI2C plug-ins before VoodooI2C):

- From this repository: `itlmt7925`, `itlmt7925Native`, `ItlXhciKick`, `MtkBtFw`, `BtLeDlFix`, `AsusWmi`, `AmdCppc`, `StrixAmp`,
  `NVRMAuxBacklight` (Lilu plugin), `AppleALC` (the layout-33 build above), `RealtekCardReader` (SD reader, built from
  `kexts/RealtekCardReader`).
- Third party (`efi/KEXTS.md` for versions): Lilu, AMFIPass, AppleMCEReporterDisabler, BrightnessKeys, NVMeFix, NullEthernet
  (+ `SSDT-RMNE`, for iCloud / App Store), RestrictEvents, VirtualSMC, SMCBatteryManager, USBToolBox + UTBDefault, VoodooI2C
  (+ Services, GPIO, Input), VoodooI2CHID, VoodooPS2Controller (keyboard plug-in only; Mouse and Trackpad disabled: the touchpad
  is I2C), BlueToolFixup, WhateverGreen.
- Not used on the GA403UM: SMCLightSensor (no light sensor), Sinetek-rtsx (attaches to the RTS525A but never works).

`Kernel/Block` keeps `com.apple.iokit.IONDRVSupport` out (the boot framebuffer would steal index 0 from NullMoth's NVRMFB).
`Misc/Tools` keeps NullMoth's recovery entries (remove the NVIDIA driver / start once without it).

boot-args:

```
keepsyms=1 -vi2c-force-polling nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth amfi_get_out_of_my_way=0x1 amfi=0x80 ipc_control_port_options=0 itlmt_fast=6 gfxrst=4 itlmt_native=1 itlmt_native_if=1 itlmt_native_dp=1 itlmt_m1=0 alcid=33 -nvrmnobootscreen
```

`alcid=33` selects the AppleALC layout; `itlmt_native*` the native Wi-Fi front end; `-vi2c-force-polling` the touchpad.
Plugin off switches: `-nvauxbloff` (brightness plugin), `-nvauxblro` (brightness plugin read-only).

Rules that kept the EFI healthy: edit `config.plist` only with `tools/occonfig.py` (each write leaves a ~57 KB backup next to
it: move those off the ESP); check `plutil -lint` and compare every copied binary with `cmp` before a restart; keep OpenCore's
file log off (`Misc/Debug/Target` 0: a DEBUG build writes 256 KB per boot); prefer Shut Down + power on to Restart.

## 6. NVIDIA driver

NullMoth 2.5.3 is installed inside macOS from <https://github.com/nullmoth/nvidia-macos-driver> (not in this repository).
`NVRMAuxBacklight` adds the brightness slider on top of it without rebuilding NullMoth: NVKMS registers no backlight for the
GA403UM's AUX-only panel (GSP brightness query status 0x56), so the plugin answers `nvkms_xnu_backlight` over DPCD
(0x721-0x723) through its own kernel RM client.

## 7. Permissions (AMFI)

With `amfi_get_out_of_my_way` macOS never shows the privacy prompt for non-Apple programs: add them by hand in System Settings
-> Privacy & Security, and again after every rebuild (ad-hoc signatures: the grant follows the binary).

| Program | Input Monitoring | Accessibility |
|---|---|---|
| `/usr/local/bin/g14-hotkeys` | yes (reads the N-KEY reports) | yes (posts the brightness keys) |
| TapGuard | | yes |
| OBS (webcam: camera) | not listed at all: start OBS's binary from Terminal (`/Applications/OBS.app/Contents/MacOS/OBS`) so the camera access is Terminal's | |

## 8. Day-to-day

- Brightness: slider and Fn+F7 / F8; the panel falls back to its own mode at boot and after display sleep, and G14Hotkeys puts
  the last level back.
- Power: RogControl -> Performance -> Manual; SPL / SPPT / FPPT go to the SMU through AmdCppc (8 W floor; the BIOS has no
  working WMI power-limit devices). CPU boost: switch under Performance mode, off again after every start.
- Speakers: RogControl -> Speakers sets the amp level (-24 ... 0 dB; -6 dB tested); the daemon re-reads it within 3 s.
- Sleep is off: `sudo pmset -a sleep 0 standby 0 powernap 0 hibernatemode 0 disablesleep 1`. Shut the laptop down before
  putting it in a bag.
- Diagnostics without root: `sysctl debug.nvauxbl`, `sysctl debug.asuswmi.status`, `sysctl debug.amdcppc.sensors`,
  `g14-gpustat -v`, `/var/log/strixamp.log`, `/var/log/g14-hotkeys.log`.

## 9. Wi-Fi notes

- eduroam: EAP-TTLS with PAP inside; the user name must include the institution's realm (`user@institution.edu`).
- A saved network failing with `0xe00002c2` is a broken macOS private-address record: forget the network and join it again.
- Joins can take more than 10 s on busy campus networks; the core now prefers the strongest access point and only takes 5 GHz
  at -70 dBm or better and within 15 dB of the 2.4 GHz one.
