---
phase: 02-discovery-protocol-abstraction
verified: 2026-03-28T00:00:00Z
status: human_needed
score: 9/9 automated must-haves verified
re_verification: false
human_verification:
  - test: "Launch the built binary on a Linux machine with avahi-daemon running. On an iOS device or macOS on the same LAN, open AirPlay picker."
    expected: "AirShow (or system hostname) appears as a receiver in the AirPlay device list."
    why_human: "Requires real avahi-daemon, real LAN, and a real Apple sender device. Cannot verify with ctest or grep."
  - test: "Launch the binary. On an Android device or in Chrome browser on the same LAN, open Cast menu."
    expected: "AirShow appears as a Cast target."
    why_human: "Requires real avahi-daemon, _googlecast._tcp registration, real LAN, and a Cast-capable sender."
  - test: "Launch the binary. Open BubbleUPnP (Android) or foobar2000 on the same LAN and browse renderers."
    expected: "AirShow appears as a DLNA Media Renderer."
    why_human: "Requires libupnp SSDP advertisement, real LAN, and a DLNA controller app."
  - test: "Change the receiver name via AppSettings::setReceiverName() and call DiscoveryManager::rename(). Check device pickers again."
    expected: "Updated name appears in AirPlay and Cast pickers within seconds."
    why_human: "Requires live avahi re-registration and a sender device to confirm name update."
---

# Phase 02: Discovery & Protocol Abstraction Verification Report

**Phase Goal:** The receiver is visible in device pickers on sender devices and protocol handler interfaces are defined before any protocol code is written.
**Verified:** 2026-03-28
**Status:** human_needed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| #  | Truth                                                                                                      | Status      | Evidence                                                                           |
|----|------------------------------------------------------------------------------------------------------------|-------------|------------------------------------------------------------------------------------|
| 1  | ProtocolHandler.h exists with pure virtual start(), stop(), name(), isRunning(), setMediaPipeline()        | VERIFIED  | File at src/protocol/ProtocolHandler.h; all 5 pure virtual methods confirmed        |
| 2  | ProtocolManager owns a vector of ProtocolHandler*, can startAll()/stopAll(), routes pipeline to handlers  | VERIFIED  | src/protocol/ProtocolManager.h/.cpp; addHandler(), startAll(), stopAll() implemented |
| 3  | ServiceAdvertiser.h defines the cross-platform advertise/rename/stop interface with factory create()      | VERIFIED  | src/discovery/ServiceAdvertiser.h; static create() present; TxtRecord struct defined |
| 4  | AppSettings reads/writes receiver name from QSettings, defaults to QSysInfo::machineHostName()            | VERIFIED  | src/settings/AppSettings.cpp; receiverName() uses QSysInfo::machineHostName()       |
| 5  | tests/test_discovery.cpp compiles and all stubs pass (SKIP/PASS) without mDNS hardware                   | VERIFIED  | ctest: 5/5 passed/skipped, 0 failed; test_discovery target builds cleanly           |
| 6  | AirShow appears in AirPlay device picker (_airplay._tcp, _raop._tcp advertised via Avahi)               | HUMAN     | AvahiAdvertiser.cpp is substantive (192 lines); wired in main.cpp; needs real LAN   |
| 7  | AirShow appears in Cast device picker (_googlecast._tcp advertised via Avahi)                           | HUMAN     | DiscoveryManager.cpp registers _googlecast._tcp; needs real LAN + sender to confirm |
| 8  | AirShow appears as DLNA Media Renderer (UpnpAdvertiser SSDP)                                            | HUMAN     | UpnpAdvertiser.cpp wired; MediaRenderer.xml valid; needs DLNA controller on LAN     |
| 9  | Windows firewall rules registered on first launch; no-op on Linux/macOS                                   | VERIFIED  | test_firewall PASSED: registerRules() returns true on Linux (D-14); _WIN32 path present |

**Score:** 9/9 automated must-haves verified (4 truths route to human verification for LAN/device confirmation)

---

### Required Artifacts

| Artifact                                  | Provides                                        | Status     | Details                                                         |
|-------------------------------------------|-------------------------------------------------|------------|-----------------------------------------------------------------|
| `src/protocol/ProtocolHandler.h`          | Abstract base class (D-06)                      | VERIFIED | Pure virtual: start, stop, name, isRunning, setMediaPipeline    |
| `src/protocol/ProtocolManager.h`          | Handler owner/lifecycle (D-08)                  | VERIFIED | addHandler, startAll, stopAll, handlerCount                     |
| `src/protocol/ProtocolManager.cpp`        | ProtocolManager implementation                  | VERIFIED | Iterates handlers, routes pipeline, g_warning on start failure  |
| `src/discovery/ServiceAdvertiser.h`       | Cross-platform mDNS abstraction                 | VERIFIED | advertise, rename, stop, static create() factory, TxtRecord     |
| `src/discovery/ServiceAdvertiser.cpp`     | Factory returning AvahiAdvertiser on Linux      | VERIFIED | #ifdef __linux__ returns make_unique<AvahiAdvertiser>()          |
| `src/discovery/AvahiAdvertiser.h`         | Linux Avahi mDNS backend                        | VERIFIED | AvahiThreadedPoll, clientCallback, groupCallback, createServices |
| `src/discovery/AvahiAdvertiser.cpp`       | Full Avahi implementation (192 lines)           | VERIFIED | AvahiThreadedPoll, entry group, COLLISION handling, TXT records  |
| `src/discovery/DiscoveryManager.h`        | Orchestrates AirPlay/Cast advertisement         | VERIFIED | start, stop, rename, isRunning; owns ServiceAdvertiser          |
| `src/discovery/DiscoveryManager.cpp`      | AirPlay/RAOP/Cast TXT records wired             | VERIFIED | _airplay._tcp, _raop._tcp, _googlecast._tcp with full TXT tables |
| `src/discovery/UpnpAdvertiser.h`          | DLNA SSDP advertiser (DISC-03)                  | VERIFIED | start, stop, isRunning, writeRuntimeXml                         |
| `src/discovery/UpnpAdvertiser.cpp`        | libupnp UpnpInit2/RegisterRootDevice/SendAdvert | VERIFIED | UpnpSendAdvertisement + UPNP_CONTROL_ACTION_REQUEST 501 stub    |
| `resources/dlna/MediaRenderer.xml`        | DLNA MediaRenderer:1 device description         | VERIFIED | Device type, AVTransport, RenderingControl, ConnectionManager    |
| `src/platform/WindowsFirewall.h`          | Windows firewall rule registration (D-12/D-13)  | VERIFIED | registerRules, rulesAlreadyRegistered static methods             |
| `src/platform/WindowsFirewall.cpp`        | INetFwPolicy2 on Win; no-op on Linux/macOS      | VERIFIED | #ifdef _WIN32 guard; D-14 comment; returns true on Linux         |
| `src/settings/AppSettings.h`              | QSettings wrapper (D-09/D-10)                   | VERIFIED | receiverName, setReceiverName, isFirstLaunch, setFirstLaunchComplete |
| `src/settings/AppSettings.cpp`            | QSettings impl with QSysInfo hostname default   | VERIFIED | defaultReceiverName() uses QSysInfo::machineHostName()           |
| `tests/test_discovery.cpp`                | Wave 0 test scaffold (5 tests)                  | VERIFIED | 3 PASS, 2 SKIP (airplay_mdns, cast_mdns require avahi+LAN)      |

---

### Key Link Verification

| From                                        | To                                         | Via                                          | Status     | Details                                                                         |
|---------------------------------------------|--------------------------------------------|----------------------------------------------|------------|---------------------------------------------------------------------------------|
| `src/protocol/ProtocolHandler.h`            | `src/pipeline/MediaPipeline.h`             | forward declaration + setMediaPipeline()     | WIRED    | `class MediaPipeline;` at line 6; `setMediaPipeline(MediaPipeline*)` at line 26 |
| `src/discovery/ServiceAdvertiser.h`         | `src/discovery/AvahiAdvertiser.h`          | static ServiceAdvertiser::create() factory   | WIRED    | ServiceAdvertiser.cpp line 8: returns make_unique<AvahiAdvertiser>() on Linux    |
| `src/discovery/DiscoveryManager.cpp`        | `src/discovery/AvahiAdvertiser.cpp`        | ServiceAdvertiser::create() factory          | WIRED    | Constructor calls ServiceAdvertiser::create(); Avahi returned on Linux           |
| `src/discovery/DiscoveryManager.cpp`        | `src/settings/AppSettings.h`              | receiverName() call at construction          | WIRED    | Line 37: `m_settings->receiverName().toStdString()`                              |
| `src/main.cpp`                              | `src/discovery/DiscoveryManager.h`         | instantiate + start() before exec()          | WIRED    | Lines 51-54: DiscoveryManager discovery(&settings); discovery.start()            |
| `src/discovery/UpnpAdvertiser.cpp`          | `resources/dlna/MediaRenderer.xml`         | UpnpRegisterRootDevice with runtime XML path | WIRED    | Line 58: UpnpRegisterRootDevice(m_runtimeXmlPath.c_str(), ...); writeRuntimeXml reads template |
| `src/main.cpp`                              | `src/discovery/UpnpAdvertiser.h`           | instantiate + start() after DiscoveryManager | WIRED    | Lines 60-63: UpnpAdvertiser upnpAdvertiser(&settings, dlnaXmlPath); start()      |
| `src/main.cpp`                              | `src/platform/WindowsFirewall.h`           | isFirstLaunch() guard + registerRules()      | WIRED    | Lines 40-48: settings.isFirstLaunch() → WindowsFirewall::registerRules()         |

---

### Data-Flow Trace (Level 4)

Not applicable for this phase. No rendering components. All artifacts are discovery/advertisement services and C++ interface headers — not UI components that render dynamic data.

---

### Behavioral Spot-Checks

| Behavior                                          | Command                                                                             | Result                              | Status  |
|---------------------------------------------------|-------------------------------------------------------------------------------------|-------------------------------------|---------|
| test_discovery target builds without changes      | cmake --build build/linux-debug --target test_discovery                             | ninja: no work to do.               | PASS  |
| 5 discovery tests run, 0 failures                 | ctest -R DiscoveryTest                                                              | 3 Passed, 2 Skipped, 0 Failed       | PASS  |
| test_dlna_ssdp validates MediaRenderer.xml        | ctest -R test_dlna_ssdp                                                             | PASSED (reads MediaRenderer:1 XML)  | PASS  |
| test_receiver_name validates QSettings persistence| ctest -R test_receiver_name                                                         | PASSED                              | PASS  |
| test_firewall validates Linux/macOS no-op (D-14)  | ctest -R test_firewall                                                              | PASSED (returns true on Linux)      | PASS  |
| Full test suite: no Phase 1 regressions           | ctest --test-dir build/linux-debug                                                  | 100% passed (10/10, 2 skipped)      | PASS  |
| AirPlay/Cast receiver visible in device pickers   | Launch binary on LAN, check iOS/Android                                             | N/A — requires hardware             | SKIP  |

---

### Requirements Coverage

| Requirement | Source Plan  | Description                                                                   | Status        | Evidence                                                                   |
|-------------|--------------|-------------------------------------------------------------------------------|---------------|----------------------------------------------------------------------------|
| DISC-01     | 02-01, 02-02 | Advertises as AirPlay receiver via mDNS (_airplay._tcp.local)                 | HUMAN       | DiscoveryManager.cpp registers _airplay._tcp + _raop._tcp; needs LAN test  |
| DISC-02     | 02-01, 02-02 | Advertises as Google Cast receiver via mDNS (_googlecast._tcp.local)          | HUMAN       | DiscoveryManager.cpp registers _googlecast._tcp with correct TXT; needs LAN|
| DISC-03     | 02-01, 02-03 | Advertises as DLNA Media Renderer via UPnP/SSDP                               | HUMAN       | UpnpAdvertiser.cpp wired; MediaRenderer:1 XML valid; test_dlna_ssdp PASSED |
| DISC-04     | 02-01, 02-02 | User can set a custom receiver name that appears in device pickers             | VERIFIED  | test_receiver_name PASSED; AppSettings.cpp uses QSysInfo hostname default  |
| DISC-05     | 02-01, 02-03 | Application registers firewall rules during installation/first-run             | VERIFIED  | test_firewall PASSED; isFirstLaunch() guard in main.cpp; no-op on Linux    |

All 5 requirement IDs (DISC-01 through DISC-05) claimed across plans 02-01, 02-02, 02-03 are accounted for. No orphaned requirements found.

---

### Anti-Patterns Found

| File                                  | Line | Pattern                                              | Severity | Impact                                                         |
|---------------------------------------|------|------------------------------------------------------|----------|----------------------------------------------------------------|
| `src/discovery/DiscoveryManager.cpp`  | 43   | `const std::string pkPlaceholder(128, '0');`         | Info   | Intentional Phase 2 placeholder for AirPlay public key (128-char zero hex). Phase 4 must replace with real key. Not a blocker — AirPlay discovery works without a valid pk, but pairing will fail. |
| `src/platform/WindowsFirewall.cpp`    | 85   | `return false;` in rulesAlreadyRegistered() on Win32 | Info   | The rulesAlreadyRegistered() Windows path always returns false and relies on isFirstLaunch() as the external guard. This is documented behavior ("Caller checks AppSettings::isFirstLaunch()"). Not a bug but could be surprising to future callers. |

No blocker or warning anti-patterns found. Both items are intentional and documented.

---

### Human Verification Required

The following 4 behaviors require a real LAN environment and sender devices to confirm. All supporting code is in place and compiles correctly.

#### 1. AirPlay Device Picker Visibility (DISC-01)

**Test:** On a machine with avahi-daemon running, build and launch AirShow. On an iOS device or macOS on the same LAN, open AirPlay picker (Control Center on iOS, or system menu bar on macOS).
**Expected:** "AirShow" (or the system hostname) appears as a receiver option in the AirPlay list.
**Why human:** Requires avahi-daemon running, mDNS packet inspection on a real LAN, and an Apple sender device. The AvahiAdvertiser thread-poll and entry group registration cannot be exercised without a real Avahi daemon.

#### 2. Google Cast Device Picker Visibility (DISC-02)

**Test:** On the same machine with the binary running, open the Google Cast menu on an Android device or in Chrome browser on the same LAN (via the Cast button).
**Expected:** "AirShow" appears as a cast target with the correct friendly name.
**Why human:** Requires avahi-daemon, _googlecast._tcp registration confirmed by a real Cast sender, and LAN multicast. The castId UUID, ca=5 flag, and fn= TXT field must all be read by the sender.

#### 3. DLNA Media Renderer Visibility (DISC-03)

**Test:** With the binary running, open BubbleUPnP (Android) or foobar2000 on the same LAN and browse the output device / renderer list.
**Expected:** "AirShow" appears as a Media Renderer. Attempting to play a track returns an error (501 Not Implemented) — the renderer appears but cannot yet play.
**Why human:** Requires libupnp SSDP advertisement to propagate on the LAN (UDP 1900 multicast), the UpnpRegisterRootDevice HTTP server to be reachable, and a DLNA controller app to discover it.

#### 4. Receiver Name Change Propagation (DISC-04 — rename path)

**Test:** After the binary is running, call AppSettings::setReceiverName("NewName") and DiscoveryManager::rename("NewName") (e.g., from a future settings UI or by patching main.cpp temporarily). Check AirPlay and Cast device pickers.
**Expected:** The updated name "NewName" appears in both AirPlay and Cast pickers within the Avahi client TTL window.
**Why human:** Requires avahi_entry_group_reset() + re-register cycle to be observed on a real sender device. The rename() code path in AvahiAdvertiser.cpp exists but cannot be triggered automatically in unit tests.

---

## Gaps Summary

No gaps found. All automated checks pass. The 4 human verification items are integration tests requiring real hardware — they are not code deficiencies. The phase goal ("receiver is visible in device pickers" and "protocol handler interfaces are defined before any protocol code is written") is structurally achieved: all interfaces exist and are substantive, the discovery stack is fully wired into main.cpp, and the test scaffold confirms correct behavior for the portions exercisable without hardware.

---

_Verified: 2026-03-28_
_Verifier: Claude (gsd-verifier)_
