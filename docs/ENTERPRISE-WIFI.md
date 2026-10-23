# Enterprise Wi-Fi (802.1X) on the native MT7925 driver

Status 2026-10-22: **WPA2-Enterprise works** (Rutgers `eduroam`, EAP-TTLS / PAP, AKM 1, CCMP): association, EAP through
eapolclient, PMK from `APPLE80211_IOC_CIPHER_KEY`, the core's 4-way handshake, eapolclient `State=Authenticated`, DHCP.
Not handled yet: re-authentication / PTK re-key on an authorised link (EAP frames after authorisation are dropped, see "Open
items"), AKM 5, PMF. Target networks: Rutgers `eduroam` and `RUWireless Secure` (WPA2-Enterprise, AES-CCMP, EAP-TTLS / PAP,
server `*.ruw.rutgers.edu`; the beacons show `8021x ft_8021x sha256_8021x`, MFP capable, not required). Branch
`wifi/gaps-24h32`, macOS 15.8.1 (24H32). Same ground rules as [NATIVE-WIFI.md](NATIVE-WIFI.md): layouts only from Apple's
binaries, no GPL source.

## How the work is split (Apple's model)

macOS runs the EAP conversation in user space (`eapolclient`, "802.1X User Mode" in its log): it reads and writes EAPOL frames
(ethertype 0x888e) on the Wi-Fi interface and, once EAP succeeds, hands the pairwise master key to the driver. The driver does
the association, carries EAPOL frames both ways while the port is not yet authorised, and runs the 4-way handshake with that
key. The core (`itlmt7925`) already has the handshake (`m3Handshake`, `itl_wpa.cpp`) for WPA2-PSK; it gets its PMK from the
native front end today as well, so the handshake does not care where the key came from.

## Phase 0 findings

### The join request (`apple80211AssocCandidates`, read by `AppleBCMWLANCore::setWCL_ASSOCIATE` 0xffffff8001608554 and `AppleBCMWLANJoinAdapter::performJoin` 0xffffff80015845b2)

| Offset | Field | Source |
|---|---|---|
| +0x0c u16 | mode (1 = IBSS) | `setWCL_ASSOCIATE` (`setAdHocCreated`) |
| +0x10 u32 | auth type lower (1 = open system) | `authtypelower2str` |
| +0x14 / +0x18 u32 | auth type upper (pair) | `authtypeupper2str`, `mapAppleUpperAuthToBcomWpaAuth` |
| +0x1c u32, +0x20 | SSID length, SSID | `setAssocSSID` |
| +0x40 | `apple80211_key` (+0x44 length, +0x48 cipher, +0x50 key) | `ciphertype2str`, `setKey` |
| +0xd4 u16, +0xd6 | RSN element built by the supplicant (length, bytes) | `getBcomPropertiesFromWPARSN`, `setAssocRSNIE` |
| +0x1e0 u16 | flags (0x2 Instant Hotspot, 0x4 Auto Instant Hotspot) | `setWCL_ASSOCIATE` log strings |
| +0x210 u32 | auth context (4th word of `IO80211AuthContext`) | `setAuthContext` |

### Captured requests (join log `itlmt_native_assoc`, native build 91A682E8)

| Network | upper | key | auth ctx | RSN element (+0xd6) |
|---|---|---|---|---|
| WPA2-PSK (hotspot, RUatHome) | 0x8 | cipher 6, 32 bytes (PMK) | 4 | `30 14 0100 000fac04 0100 000fac04 0100 000fac02 0000` |
| eduroam | **0x800** | none (cipher 0, 0 bytes) | 0 | `30 14 0100 000fac04 0100 000fac04 0100 000fac`**`05`**` 0000` |

So an 802.1X join is recognisable without guessing (upper 0x800, no key, AKM in the supplicant's RSN element), and the
supplicant picked **AKM 5 (802.1X-SHA256)** because the AP offers it. AKM 5 changes the 4-way handshake (KDF-SHA256 for the
PTK, AES-128-CMAC MIC, key descriptor version 3); AKM 1 (802.1X) uses exactly the HMAC-SHA1 / version 2 handshake the core
already has. The AP offers AKM 1 as well.

## Phases

Each phase ends with one boot and a fixed list of things to read (join log, `itlmt_native_calls`, `/usr/bin/log` for airportd /
eapolclient). The PSK and open paths must stay unchanged; every new branch is entered only for upper 0x800 requests.

### Phase 1: associate as 802.1X (AKM 1), log what follows

* Native: an upper 0x800 request without a key becomes a join of kind "8021x" (join struct version 3, `kind` field; a
  version 2 core refuses it).
* Core: association request carries an RSN element with AKM 00-0F-AC:1 (CCMP / CCMP, RSN caps 0), PRIVACY set; no PMK, no
  handshake started yet; data path open for 0x888e only.
* Exit: association accepted by the eduroam AP; the call log shows which ioctls eapolclient sends next (expected: the PMK
  through `CIPHER_KEY` 0x3 or a PMK ioctl), and whether airportd wants a link-up event before EAP starts.

### Phase 2: EAPOL both ways while unauthorised

* Core RX: EAPOL type 0 (EAP packet) and type 1 / 2 go to the host interface even though the link is not "up"; EAPOL-Key
  (type 3) stays with the core.
* Core TX: frames with ethertype 0x888e from the host are sent unprotected before the keys exist (today `ethTx` requires
  `m3linkUp`); everything else is still dropped until the port is authorised.
* Native: link reported up after association for an 8021x join, as Apple's driver does (the port is not authorised yet).
* Exit: eapolclient log shows the TTLS exchange and "authenticated" (EAP success from the RADIUS server).

### Phase 3: key from the supplicant, 4-way handshake

* Native: accept the PMK eapolclient hands over (ioctl from phase 1), pass it to the core.
* Core: run `m3Handshake` with it when message 1 arrives (buffer message 1 if it comes before the key).
* Exit: keys installed, DHCP address on eduroam, browsing works; re-authentication after the AP's timeout works.

### Phase 4: AKM 5 and PMF (optional)

* KDF-SHA256 + AES-CMAC (descriptor version 3) in `itl_wpa.cpp` with host tests, then keep the supplicant's AKM 5 element.
* PMF (802.11w, BIP / IGTK) only if a target network requires it; Rutgers' do not.

## Risks

| Risk | Mitigation |
|---|---|
| airportd waits for a link state the driver never posts, joins hang | phase 1 only logs; the fast "unsupported" path stays for everything not 0x800 |
| macOS checks that the association used its RSN element (AKM 5) | if the AKM 1 element is rejected, phase 4 moves forward |
| EAPOL to the host before link-up breaks the Skywalk data path assumptions | only 0x888e is passed; the PSK path keeps its current order |
| The RADIUS server rejects the identity / certificate | that is configuration (JoinNow profile), visible in the eapolclient log, not the driver |

## What it took (2026-10-22, phases 1 to 3 in one branch)

| Piece | Where | Source of the fact |
|---|---|---|
| upper auth 0x800 + no key = 802.1X join, join struct version 3 `eap` | `itlmt7925_iface.cpp` setWCL_ASSOCIATE | captured requests (table above) |
| association with RSN AKM 1, PRIVACY, link up unauthorised, watchdog off | core `m3Run1` / `m3Assoc` | 802.11 |
| EAP (EAPOL type != 3) to the host, only 0x888e transmitted until authorised | core `m3RxData` / `ethTx` | 802.1X port model |
| `setCIPHER_KEY`: +4 length, +8 cipher, +0xc index, +0xe flags, +0x10 key; cipher 9, 32 bytes = PMK | native | `AppleBCMWLANCore::setCIPHER_KEY` 0xffffff800160dc76 log format |
| PMK -> core (`ITL_NATIVE_PMK`), handshake in the link loop, M2 RSN element with AKM 1 | core | existing `m3Handshake` |
| "handshake complete" to eapolclient = `messageClients(kIOMessageServicePropertyChange)` on the native interface | native `pollControl` | eapolclient: `IOServiceAddInterestNotification(IOGeneralInterest)` on the 2nd parent of en4, callback compares 0xe0000130 |

Dead ends worth remembering: event 0xa is APPLE80211_M scan done (`AppleBCMWLANCore::scanComplete`), not a handshake event;
`handlePSKEvent` posts 0x4c (40-byte status) for the firmware supplicant, which eapolclient does not use. Disconnect reason 23
from the AP before any EAPOL-Key = the RADIUS server rejected the login (eduroam needs `NetID@rutgers.edu`).

Logs: core `wifi-8021x-log` (association, PMK, handshake summary, authorisation, link end), native `itlmt_native_assoc`
(requests, cipher keys, the property-change message), eapolclient `log show --info --predicate 'process == "eapolclient"'`.

## Open items

* Re-authentication on an authorised link: the core drops EAP frames once `m3eapAuth` is set and does not run a second 4-way
  handshake (PTK re-key) or a group key handshake on an 802.1X link; a RADIUS session timeout will end in a disconnect and a
  fresh join.
* AKM 5 (the supplicant's choice) and PMF, phase 4.
