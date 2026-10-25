# GA403UM Hackintosh

macOS Sequoia on the **ASUS ROG Zephyrus G14 (2025) GA403UM**: Ryzen 9 270 ("Hawk Point", Zen 4) + RTX 5060 Laptop,
2880 × 1800 OLED. OpenCore EFI, custom drivers and utilities, and everything needed to build them from this one repository.

## Origin and credit

This repository is a GA403UM continuation of **[thanhnndev/ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh)**,
the GA403WM (Ryzen AI 9 HX 370) project by [thanhnndev](https://github.com/thanhnndev). The Wi-Fi, Bluetooth, USB, ASUS and
CPU drivers, RogControl, TapGuard, the speaker-amp research, the build system and most of the documentation are theirs; the
full git history is kept, so every original commit stays credited to its author. Copyright notices and the licences below are
unchanged. The GA403UM work on top of it (native Wi-Fi for open and WPA2-Enterprise networks, the brightness slider plugin for
NullMoth, Fn-key and ROG-key helper, CPU power and temperature limits through the SMU, keyboard colour, GPU status, the speaker
bring-up on the UM and the rest listed in [docs/GA403UM-SETUP.md](docs/GA403UM-SETUP.md)) is by
[joemens21](https://github.com/joemens21). Not an official release of the original project; problems with this repository
belong here, not upstream.

## Vibe-coded project — read before installing

This is a vibe-coded, experimental project. **Almost all code and documentation was AI-written (Claude)**, directed and tested by one person per machine, with no independent audit.

- Kexts run in the kernel; bugs can cause panics, hangs or data loss. `AsusWmi` and `AmdCppc` write to the EC, BIOS through SMI, CPU registers and the SMU.
- EFI settings are adapted from other projects: SIP is mostly disabled, AMFI is relaxed and Secure Boot is off.
- Injected Wi-Fi passwords (legacy Wi-Fi mode) are stored **in plain text inside the kext**. Never share a credential-injected kext. Logs also contain MAC addresses and nearby SSIDs/BSSIDs; redact them before posting.
- Back up your disk and keep Windows/Linux recovery media. Use a machine you can reinstall, and avoid work you cannot afford to lose. No warranty; see [LICENSE](LICENSE).

The repo ships empty `itl_ssid*/itl_psk*` fields and `CHANGE_ME_*` SMBIOS placeholders. CI checks for real credentials and identifiers.

## Status (GA403UM, October 2026)

macOS **15.8.1 (24H32)** only, OpenCore 1.0.9 (Zen 5 timer patch), NullMoth NVIDIA driver **2.5.3**. macOS 26 would lose Wi-Fi
and Bluetooth until they are ported ([docs/TAHOE-MIGRATION.md](docs/TAHOE-MIGRATION.md)).

- [x] **Wi-Fi (native):** WPA2-PSK, open and WPA2-Enterprise (eduroam) from System Settings; strongest access point chosen
- [x] **Bluetooth:** classic and BLE
- [x] **Speakers:** both CS35L56 amps and the woofer, amp level adjustable in RogControl
- [x] **OLED brightness:** macOS slider and Fn+F7 / F8 together
- [x] **Fn keys / ROG key:** keyboard light, Slash, mute, mic, brightness; ROG key opens RogControl
- [x] **ROG controls:** profiles, CPU power limits and temperature target, fan curves, charge limit, keyboard colour and effects, CPU boost switch (off by default), GPU status, history graphs
- [x] **Webcam:** 640 × 480 through the OBS virtual camera
- [ ] **HDMI hot-plug:** connect HDMI before power-on (NullMoth issue)
- [ ] **Sleep / wake, AirDrop, WPA3:** not working; sleep is turned off
- [x] **SD reader (Realtek RTS525A):** our build of FireWolf's RealtekCardReader with a power-up delay and CMD8 retries; a 256 GB SDXC card mounts

## Install and build

**[docs/GA403UM-SETUP.md](docs/GA403UM-SETUP.md)** is the complete guide: build prerequisites, one command per component, the
EFI kext list and boot-args, the macOS-side daemons and the permissions to add by hand. In short:

```bash
bash scripts/build.sh
```

builds every kext into `build/`; RogControl, the Fn-key helper, the speaker daemon and the boost switch each have their own
`scripts/build-*.sh` / `scripts/macos-*-install.sh`. Scripts are not executable: run them through `bash`.

The upstream GA403WM documentation is kept in `docs/` (installation from scratch, [INSTALL.md](docs/INSTALL.md); EFI,
[efi/README.md](efi/README.md); component reference, [COMPONENTS.md](docs/COMPONENTS.md); [ROADMAP.md](docs/ROADMAP.md)).
Where it says GA403WM / Strix Point, check the GA403UM guide for the difference. [Tiếng Việt](README.vi.md) is the upstream
GA403WM README.

```text
efi/                OpenCore reference config, SSDTs, third-party kext list
kexts/              Wi-Fi, USB, ASUS controls, CPU, Bluetooth, speaker amps, brightness plugin, diagnostics
macos/              RogControl, G14Hotkeys, g14-gpustat, BoostSwitch, TapGuard, ItlWiFi, AmdCppcPolicy
fw/                 MT7925 and CS35L56 firmware with their licences
patches/            OpenCore Zen 5 patch, AppleALC GA403 layout
scripts/            Build, install, EFI export/privacy check
tools/              occonfig, kernel-collection reader, probes, test tools
docs/               Guides, component reference, research and test notes
```

## License

Code uses the [Clear BSD License](LICENSE) (Copyright thanhnndev and contributors), including reimplementations of MediaTek's
Linux `mt76` (BSD-3-Clause-Clear). Firmware has its vendors' licences ([MediaTek](fw/LICENCE.mediatek), Cirrus Logic in
`fw/cs35l56`). **The EFI derives from `nullmoth/1401` and uses PolyForm Noncommercial 1.0.0: noncommercial use only.** Full
credits and licences: [NOTICE.md](NOTICE.md).

Thanks to thanhnndev for the project this builds on, [NullMoth](https://github.com/nullmoth/1401) for the EFI base and NVIDIA
driver, Acidanthera for OpenCore/Lilu/VirtualSMC/AppleALC, FireWolf for RealtekCardReader, and Linux `mt76`, `btmtk`,
`asus-wmi`, `asusctl`, `cs35l56` and RyzenAdj for design references.

Not affiliated with ASUS, Apple, NVIDIA, MediaTek, AMD, Cirrus Logic, NullMoth or the original project. Trademarks belong to
their owners; users are responsible for complying with Apple's macOS license terms.
