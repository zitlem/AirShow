# Phase 2: Discovery & Protocol Abstraction - Research

**Researched:** 2026-03-28
**Domain:** mDNS/Bonjour service advertisement, UPnP/SSDP, protocol handler abstraction, QSettings, Windows Firewall COM API
**Confidence:** HIGH (Avahi, libupnp, QSettings all well-documented; AirPlay TXT records MEDIUM due to Apple's unofficial spec)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Use platform-native mDNS libraries with a thin C++ abstraction layer: Avahi (via libavahi-client) on Linux, dns_sd (built-in) on macOS, Bonjour SDK for Windows
- **D-02:** Advertise AirPlay as `_airplay._tcp.local` with required TXT records (deviceid, features, model, srcvers)
- **D-03:** Advertise Google Cast as `_googlecast._tcp.local` with required TXT records (id, cd, md, fn, rs, st)
- **D-04:** Advertise DLNA via UPnP/SSDP using libupnp (pupnp) — DLNA uses SSDP discovery, not mDNS
- **D-05:** All advertisements use the same user-configurable receiver name
- **D-06:** Define an abstract `ProtocolHandler` base class in `src/protocol/ProtocolHandler.h` with virtual methods: `start()`, `stop()`, `name()`, `isRunning()`
- **D-07:** Each protocol handler will feed decoded media data into the shared `MediaPipeline` via its `appsrc` injection point (from Phase 1 D-05)
- **D-08:** A `ProtocolManager` class owns all registered `ProtocolHandler` instances, starts/stops them as a group, and routes session events
- **D-09:** Store receiver name in QSettings (platform-native: registry on Windows, plist on macOS, XDG config on Linux)
- **D-10:** Default name is the system hostname. User can change it via a settings mechanism (exact UI deferred to Phase 3)
- **D-11:** When name changes, all active service advertisements must be re-registered immediately
- **D-12:** On Windows, register firewall rules at runtime using the Windows Firewall COM API (INetFwPolicy2) on first launch
- **D-13:** If elevated permissions are unavailable, display a user-friendly prompt explaining which ports need opening
- **D-14:** On Linux and macOS, rely on system defaults (mDNS typically works without firewall changes)

### Claude's Discretion

- Exact TXT record values for AirPlay and Cast advertisements (research will determine current required fields)
- UPnP device description XML structure for DLNA DMR
- Whether to use a ServiceAdvertiser abstraction or separate classes per discovery protocol
- Internal threading model for discovery services

### Deferred Ideas (OUT OF SCOPE)

None — discussion stayed within phase scope
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DISC-01 | Application advertises as an AirPlay receiver via mDNS (`_airplay._tcp.local`) | Avahi API documented; TXT record fields researched from openairplay spec and UxPlay source |
| DISC-02 | Application advertises as a Google Cast receiver via mDNS (`_googlecast._tcp.local`) | Cast TXT fields documented from oakbits.com and Cisco reference; same Avahi API as DISC-01 |
| DISC-03 | Application advertises as a DLNA Media Renderer via UPnP/SSDP | libupnp 1.14.24 available in apt; DMR device description XML structure documented |
| DISC-04 | User can set a custom receiver name that appears in device pickers | QSettings NativeFormat stores per-platform; Avahi supports re-registration on name change |
| DISC-05 | Application registers firewall rules during installation/first-run so discovery works without manual config | INetFwPolicy2 COM API documented; UDP 5353 + required TCP ports to register |
</phase_requirements>

---

## Summary

Phase 2 establishes the discovery layer — the mechanism by which senders (iOS, Android, DLNA controllers) find the receiver on the local network. It also defines the `ProtocolHandler` / `ProtocolManager` abstraction that all future protocol phases will implement against. No media flows in this phase; the success criterion is purely that the receiver appears in device pickers.

Three independent discovery mechanisms must be wired up: mDNS/Bonjour for AirPlay and Google Cast, UPnP/SSDP for DLNA. On Linux the Avahi daemon (already active on this machine) handles mDNS; on macOS the native `dns_sd.h` API (Bonjour) is used; on Windows the Bonjour SDK (Apache-licensed mDNSResponder source) must be bundled or assumed present. The platform difference is best hidden behind a `ServiceAdvertiser` abstract class so protocol-specific code calls a single `advertise(type, name, port, txtRecords)` interface.

The Windows firewall requirement (DISC-05) requires COM elevation awareness: `INetFwPolicy2` can add rules from a non-elevated process via the COM elevation moniker, but this triggers a UAC prompt. The fallback (D-13) must display an actionable message listing the exact ports and protocols to open manually.

**Primary recommendation:** Implement a `ServiceAdvertiser` abstraction with platform-specific backends (Avahi/dns_sd/Bonjour SDK). This keeps the `DiscoveryManager` — which registers AirPlay, Cast, and DLNA service records — free of `#ifdef` blocks. The `ProtocolHandler` interface is a pure C++ header; stub implementations for testing require no real protocol code.

---

## Standard Stack

### Core (Phase 2 additions to existing Phase 1 stack)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| **libavahi-client** | 0.8 (available in apt) | mDNS service advertisement on Linux | Used by UxPlay, shairport-sync, and every serious Linux AirPlay receiver. LGPL. avahi-daemon already active on this machine. |
| **libavahi-compat-libdnssd** | 0.8 | `dns_sd.h` compatibility shim on Linux | Allows writing code against Apple's `DNSServiceRegister` API that compiles on all three platforms with the same header. Avahi provides this compat layer. |
| **dns_sd (built-in)** | macOS system | mDNS on macOS | Zero-install — Bonjour is part of macOS. Use `DNSServiceRegister()` from `<dns_sd.h>`. |
| **mDNSResponder (Apple OSS)** | 1556.60.9+ | mDNS on Windows | Apache 2.0 license. Source available from Apple's open source repository. The Bonjour SDK for Windows (v3.0, 2011) is the last official SDK but mDNSResponder source is more current. Build the `mDNSWindows` target or bundle the installer. |
| **libupnp (pupnp)** | 1.14.24 (available in apt) | UPnP/SSDP advertisement and HTTP server for DLNA | Active maintenance (GitHub: pupnp/pupnp). Only viable cross-platform UPnP SDK for C. Implements SSDP multicast, HTTP device description serving, and SOAP action handling. |
| **Qt6::Core (QSettings)** | 6.9.2 (already linked) | Receiver name persistence | Already a project dependency. `QSettings` with `NativeFormat` uses registry (Windows), plist (macOS), XDG config file (Linux). No additional library needed. |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| **netfw.h / ole32.lib** | Windows SDK (built-in) | Windows Firewall COM API | Windows platform only, first-launch firewall rule registration |
| **GTest / GMock** | 1.17.0 (already in tests/) | Unit tests for handler interface and advertiser logic | Phase test targets — existing tests/CMakeLists.txt pattern |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| libavahi-client (Linux) | mjansson/mdns (header-only) | mjansson/mdns is simpler but requires owning the UDP socket and re-announcement loop manually. Avahi daemon handles re-announcement and conflict resolution automatically. For production use, Avahi is correct. |
| libupnp (pupnp) | libnpupnp | libnpupnp is a cleaner rewrite with fewer historical bugs but sparse documentation and a smaller user base for v1. Upgrade path is viable in v2. |
| dns_sd.h compat layer (cross-platform) | Avahi-native API on Linux + dns_sd on macOS/Windows separately | More code paths to test; no shared interface. The compat layer (`libavahi-compat-libdnssd-dev`) is the standard approach used by shairport-sync and UxPlay. |

**Installation (Linux dev machine — packages available in apt):**
```bash
sudo apt install \
  libavahi-client-dev \
  libavahi-compat-libdnssd-dev \
  libupnp-dev
```

macOS:
```bash
brew install avahi libupnp   # avahi not usually needed; dns_sd.h is built-in
```

Windows: Build mDNSResponder from Apple's open source repository (`mDNSWindows` target) or use the Bonjour SDK installer approach; bundle the resulting `dnssd.dll`.

---

## Architecture Patterns

### Recommended Project Structure (Phase 2 additions)

```
src/
├── pipeline/               # (Phase 1 — unchanged)
│   ├── MediaPipeline.h
│   └── MediaPipeline.cpp
├── protocol/               # NEW in Phase 2
│   ├── ProtocolHandler.h   # Abstract base class (D-06)
│   └── ProtocolManager.h / .cpp  # Owner + lifecycle coordinator (D-08)
├── discovery/              # NEW in Phase 2
│   ├── ServiceAdvertiser.h        # Abstract: advertise/stop/rename
│   ├── AvahiAdvertiser.h / .cpp   # Linux backend (libavahi-client)
│   ├── DnsSdAdvertiser.h / .cpp   # macOS backend (dns_sd.h built-in)
│   ├── BonjourAdvertiser.h / .cpp # Windows backend (dnssd.dll)
│   ├── DiscoveryManager.h / .cpp  # Owns ServiceAdvertiser; registers all service records
│   └── UpnpAdvertiser.h / .cpp    # UPnP/SSDP for DLNA via libupnp
├── settings/               # NEW in Phase 2
│   └── AppSettings.h / .cpp  # QSettings wrapper: receiver name, first-run flag
├── platform/               # NEW in Phase 2
│   └── WindowsFirewall.h / .cpp  # INetFwPolicy2 rule registration (Windows only)
└── ui/                     # (Phase 1 — unchanged)
```

### Pattern 1: ServiceAdvertiser Abstraction

**What:** A single abstract interface hides all three mDNS backend implementations. `DiscoveryManager` calls `advertise()` once per service type and never touches platform-specific code.

**When to use:** Always — this keeps DISC-01/DISC-02 logic in `DiscoveryManager` and platform differences isolated to three .cpp files.

**Example:**
```cpp
// src/discovery/ServiceAdvertiser.h
// Source: Pattern from shairport-sync / UxPlay cross-platform discovery layer
namespace airshow {

struct TxtRecord {
    std::string key;
    std::string value;
};

class ServiceAdvertiser {
public:
    virtual ~ServiceAdvertiser() = default;

    // Advertise a DNS-SD service. Returns false if advertisement fails.
    virtual bool advertise(const std::string& serviceType,
                           const std::string& name,
                           uint16_t port,
                           const std::vector<TxtRecord>& txt) = 0;

    // Re-register all active advertisements with a new name.
    virtual bool rename(const std::string& newName) = 0;

    // Stop all advertisements.
    virtual void stop() = 0;

    // Factory: returns the correct backend for the current platform.
    static std::unique_ptr<ServiceAdvertiser> create();
};

} // namespace airshow
```

### Pattern 2: ProtocolHandler Abstract Base Class

**What:** The `ProtocolHandler` base class defines the lifecycle interface that all future protocol implementations (AirPlay Phase 4, DLNA Phase 5, Cast Phase 6) will implement. Phase 2 defines the interface only — no implementations.

**When to use:** Define now, implement in later phases. The interface must be stable because `ProtocolManager` is written against it in this phase.

**Example:**
```cpp
// src/protocol/ProtocolHandler.h
// Source: Architecture decisions D-06 through D-08 from CONTEXT.md
namespace airshow {

class MediaPipeline;  // forward declare

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    virtual bool start()   = 0;  // Begin listening for connections
    virtual void stop()    = 0;  // Tear down all active sessions
    virtual std::string name() const = 0;  // "airplay", "cast", "dlna"
    virtual bool isRunning() const   = 0;

    // Called by ProtocolManager when pipeline is available for use.
    // Handler stores the pointer; does NOT own it.
    virtual void setMediaPipeline(MediaPipeline* pipeline) = 0;
};

} // namespace airshow
```

### Pattern 3: Avahi Threaded Poll (Linux mDNS)

**What:** Avahi's `AvahiThreadedPoll` runs the mDNS event loop on a background thread. The main thread creates services via `avahi_entry_group_add_service_strlst()` and commits them. Thread safety: always lock `avahi_threaded_poll_get()`'s mutex before touching the entry group.

**When to use:** Linux backend. The threaded poll is the right choice for a Qt application — it does not block the Qt event loop.

**Example:**
```cpp
// Source: Avahi client-publish-service example (avahi.org/doxygen/html/client-publish-service_8c-example.html)
// and shairport-sync mdns_avahi.c pattern

AvahiThreadedPoll*  m_poll   = nullptr;
AvahiClient*        m_client = nullptr;
AvahiEntryGroup*    m_group  = nullptr;

// On create_services() callback (called from Avahi thread):
avahi_entry_group_add_service_strlst(
    m_group,
    AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
    (AvahiPublishFlags)0,
    m_name.c_str(),
    "_airplay._tcp",
    nullptr, nullptr,
    AIRPLAY_PORT,
    buildAirPlayTxt()   // returns AvahiStringList*
);
avahi_entry_group_commit(m_group);
```

### Pattern 4: Avahi Name Collision Handling

**What:** When two receivers on the same LAN use the same name, Avahi calls the entry group callback with `AVAHI_ENTRY_GROUP_COLLISION`. The correct response is to generate an alternative name with `avahi_alternative_service_name()` and re-register.

**When to use:** Must implement — skip this and two instances on the same LAN will silently fail to advertise.

**Example:**
```cpp
// Source: Avahi example client-publish-service.c
case AVAHI_ENTRY_GROUP_COLLISION: {
    char* alt = avahi_alternative_service_name(m_name.c_str());
    m_name = alt;
    avahi_free(alt);
    create_services(client);  // re-register with new name
    break;
}
```

### Pattern 5: libupnp DLNA Device Advertisement

**What:** `UpnpInit2()` starts the UPnP stack, `UpnpRegisterRootDevice()` serves the device description XML, and `UpnpSendAdvertisement()` broadcasts SSDP NOTIFY messages. A callback handles SOAP action requests (AVTransport, RenderingControl) — in Phase 2 these return `501 Not Implemented` stubs.

**When to use:** DLNA SSDP advertisement (DISC-03). The same setup is extended in Phase 5 with real AVTransport logic.

**Example:**
```cpp
// Source: pupnp/upnp/sample pattern (github.com/pupnp/pupnp)
UpnpInit2(nullptr, 0);   // nullptr = all interfaces, 0 = auto port
UpnpRegisterRootDevice(
    "/path/to/MediaRenderer.xml",
    upnpCallbackHandler,   // handles actions in Phase 5
    nullptr,
    &m_deviceHandle
);
UpnpSendAdvertisement(m_deviceHandle, 100);  // 100s TTL
```

### Pattern 6: QSettings Receiver Name

**What:** `QSettings` with no arguments and `QCoreApplication::organizationName()` / `applicationName()` set uses the platform-native store automatically. No format argument needed.

**When to use:** Receiver name persistence (D-09). The default name is `QSysInfo::machineHostName()`.

**Example:**
```cpp
// Source: Qt 6 documentation (doc.qt.io/qt-6/qsettings.html)
// In AppSettings constructor — called once at startup
QSettings settings;  // NativeFormat, org/app from QCoreApplication
QString name = settings.value("receiver/name",
                               QSysInfo::machineHostName()).toString();

// On name change:
settings.setValue("receiver/name", newName);
settings.sync();
// Then call discoveryManager->rename(newName) to re-register advertisements
```

### Pattern 7: Windows Firewall Rule Registration

**What:** `INetFwPolicy2` adds inbound allow rules for the ports discovery requires. Must be called before starting the mDNS/UPnP stack on Windows first launch. If `CoCreateInstance` returns `E_ACCESSDENIED`, fall through to the user-facing fallback message (D-13).

**When to use:** Windows platform only, on first-run flag from QSettings.

**Example:**
```cpp
// Source: Microsoft Learn — Adding an Outbound Rule
// (learn.microsoft.com/en-us/previous-versions/windows/desktop/ics/c-adding-an-outbound-rule)
// and Windows-classic-samples EdgeTraversalOptions
HRESULT addFirewallRule(const wchar_t* name, NET_FW_IP_PROTOCOL proto,
                        const wchar_t* ports) {
    INetFwPolicy2* pPolicy = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2),
                                  reinterpret_cast<void**>(&pPolicy));
    if (FAILED(hr)) return hr;  // E_ACCESSDENIED → show D-13 prompt

    INetFwRules* pRules = nullptr;
    pPolicy->get_Rules(&pRules);

    INetFwRule* pRule = nullptr;
    CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                     __uuidof(INetFwRule),
                     reinterpret_cast<void**>(&pRule));
    pRule->put_Name(SysAllocString(name));
    pRule->put_Protocol(proto);
    pRule->put_LocalPorts(SysAllocString(ports));
    pRule->put_Direction(NET_FW_RULE_DIR_IN);
    pRule->put_Action(NET_FW_ACTION_ALLOW);
    pRule->put_Enabled(VARIANT_TRUE);

    pRules->Add(pRule);
    // Release COM objects...
    return S_OK;
}
```

### Anti-Patterns to Avoid

- **Platform `#ifdef` inside DiscoveryManager:** Keep platform conditionals inside the `ServiceAdvertiser` backend classes only. `DiscoveryManager` is platform-agnostic.
- **Calling `avahi_entry_group_add_service()` from the Qt main thread:** Avahi's threaded poll owns the event loop; all entry group operations must happen from within the Avahi callback thread or while the poll is locked with `avahi_threaded_poll_lock()`.
- **Using a blocking Avahi poll:** `avahi_simple_poll_loop()` blocks — use `avahi_threaded_poll_new()` for a Qt application.
- **Starting discovery before QSettings loads the receiver name:** The name must be resolved from QSettings before any mDNS registration so the correct name appears on first advertisement.
- **Not handling `AVAHI_ENTRY_GROUP_COLLISION`:** Two instances of AirShow on the same LAN will silently fail to advertise if collision is not handled.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| mDNS multicast socket management | Custom UDP 5353 multicast sender | libavahi-client (Linux) / dns_sd.h (macOS/Windows) | mDNS requires IPv4 AND IPv6 multicast groups, re-announcement every TTL/2, conflict detection with tie-breaking rules, and correct packet framing. The full spec (RFC 6762) is 70 pages. |
| SSDP multicast announcement | Custom UDP 239.255.255.250:1900 sender | libupnp `UpnpSendAdvertisement()` | SSDP requires periodic NOTIFY (alive), NOTIFY (byebye) on shutdown, response to M-SEARCH, correct USN formatting, and an HTTP server for device description XML. pupnp handles all of this. |
| UPnP device description HTTP server | Custom HTTP server serving XML | libupnp built-in HTTP server | libupnp registers a root device and automatically serves the description XML at the registered URL. |
| Platform-native settings storage | Custom config file parser | QSettings NativeFormat | Already linked. Handles Windows registry ACLs, macOS plist encoding, and Linux XDG path resolution correctly. |
| mDNS name collision resolution | Custom tie-breaking logic | Avahi's `avahi_alternative_service_name()` | RFC 6762 tie-breaking is non-trivial; Avahi implements it correctly. |

**Key insight:** The discovery layer is almost entirely about multicast protocol state machines — re-announcement timers, conflict resolution, packet format. These are exactly the kinds of problems that appear simple but have many edge cases that existing libraries have already solved.

---

## AirPlay TXT Record Reference

This section is Claude's Discretion — researched to give the planner exact values.

### `_airplay._tcp` Required TXT Fields

| Field | Example Value | Notes |
|-------|--------------|-------|
| `deviceid` | `AA:BB:CC:DD:EE:FF` | MAC address of the host NIC. Must be stable across restarts. |
| `features` | `0x5A7FFFF7,0x1E` | 64-bit feature bitfield (two 32-bit hex values separated by comma). The high/low split is required for modern iOS. Use values from UxPlay's `dnssd_set_airplay_features()` as the starting point — it enables bits verified against current iOS. |
| `model` | `AppleTV3,2` | Shown in iOS AirPlay picker. Any AppleTV model string works. |
| `srcvers` | `220.68` | AirPlay server version string. Match UxPlay's current value. |
| `pk` | 64-byte hex string | Public key for pairing (Phase 4). For Phase 2 (discovery only), advertise a placeholder 64-byte zero-padded hex string — iOS will see the receiver but cannot complete pairing until Phase 4. |
| `pi` | UUID string | Group/pairing identifier. Generate once at install; store in QSettings. |

**Note on `features` value:** The `0x5A7FFFF7,0x1E` value is from the UxPlay codebase (antimof/UxPlay uxplay.cpp) and represents the set of feature bits that modern iOS versions accept. Bit 7 (mirroring), bit 9 (audio), bit 30 (RAOP), and bit 27 (legacy pairing) are the critical ones for discovery and mirroring. Confidence: MEDIUM — these values are from reverse engineering, not Apple documentation. Copy UxPlay's current source values rather than hardcoding a stale number.

### `_raop._tcp` Required TXT Fields

Must also advertise `_raop._tcp` — iOS uses this for audio and legacy mirroring discovery. Service name format: `<MAC>@<ReceiverName>._raop._tcp.local`.

| Field | Value | Notes |
|-------|-------|-------|
| `txtvers` | `1` | Always 1 |
| `ch` | `2` | Stereo audio channels |
| `cn` | `0,1,2,3` | Audio codecs: PCM, ALAC, AAC, AAC-ELD |
| `da` | `true` | Device authentication |
| `et` | `0,3,5` | Encryption types |
| `md` | `0,1,2` | Metadata types |
| `pw` | `false` | No password required initially |
| `sr` | `44100` | Sample rate |
| `ss` | `16` | Sample size bits |
| `tp` | `UDP` | Transport |
| `vn` | `65537` | AirTunes protocol version |
| `vs` | `220.68` | AirPlay version (match srcvers above) |
| `am` | `AppleTV3,2` | Device model (match _airplay._tcp model) |
| `pk` | same as _airplay._tcp | Same public key placeholder |
| `ft` | `0x5A7FFFF7,0x1E` | Feature flags (same value as _airplay._tcp features) |

### `_googlecast._tcp` Required TXT Fields

| Field | Value | Notes |
|-------|-------|-------|
| `id` | UUID (no hyphens) | Unique device identifier. Generate once; store in QSettings. |
| `ve` | `02` | Protocol version — always `02` |
| `md` | `AirShow` | Model name shown in Cast UI |
| `fn` | `<ReceiverName>` | Friendly name — this is what appears in the Cast menu |
| `ic` | `/icon.png` | Icon path served by the Cast HTTP server (stub for Phase 2) |
| `ca` | `5` | Capability flags: 1=VIDEO, 4=AUDIO; value 5 = video+audio |
| `st` | `0` | Status: 0 = idle |
| `rs` | `` | Running app (empty when idle) |

**Note:** `cd` (certificate data) appears in some Cast devices but is not required for the receiver to appear in device pickers. It is part of the Cast authentication flow — out of scope for Phase 2.

---

## DLNA UPnP Device Description XML

This section is Claude's Discretion — researched structure for planning.

A minimal DLNA MediaRenderer device description XML must declare device type `urn:schemas-upnp-org:device:MediaRenderer:1` and expose three required services:

```xml
<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>
    <friendlyName><!-- receiver name --></friendlyName>
    <manufacturer>AirShow Project</manufacturer>
    <modelName>AirShow</modelName>
    <UDN>uuid:<!-- stable UUID generated at install --></UDN>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>
        <controlURL>/upnp/control/avt</controlURL>
        <eventSubURL>/upnp/event/avt</eventSubURL>
        <SCPDURL>/avt-scpd.xml</SCPDURL>
      </service>
      <service>
        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>
        <controlURL>/upnp/control/rc</controlURL>
        <eventSubURL>/upnp/event/rc</eventSubURL>
        <SCPDURL>/rc-scpd.xml</SCPDURL>
      </service>
      <service>
        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>
        <controlURL>/upnp/control/cm</controlURL>
        <eventSubURL>/upnp/event/cm</eventSubURL>
        <SCPDURL>/cm-scpd.xml</SCPDURL>
      </service>
    </serviceList>
  </device>
</root>
```

In Phase 2, all SOAP actions (SetAVTransportURI, Play, Stop, etc.) return `501 Not Implemented`. This is sufficient for DLNA controllers to discover and display the renderer — they just cannot issue play commands yet. That is correct for Phase 2.

---

## Common Pitfalls

### Pitfall 1: avahi-daemon Not Running

**What goes wrong:** `avahi_client_new()` fails silently or returns `AVAHI_CLIENT_FAILURE`. The receiver never appears in AirPlay/Cast menus. No user-visible error.

**Why it happens:** On some Linux configurations, avahi-daemon is installed but disabled. On the current dev machine it is active, but users may have it disabled.

**How to avoid:** At startup, check `avahi_client_get_state()` returns `AVAHI_CLIENT_S_RUNNING`. If not, emit `qCritical` with actionable message: "avahi-daemon is not running. Start it with: `sudo systemctl start avahi-daemon`".

**Warning signs:** `DiscoveryManager::start()` returns false but no protocol error is logged. Check avahi-daemon status first.

### Pitfall 2: Calling Avahi API from Qt Main Thread

**What goes wrong:** Race condition — Qt event loop and Avahi event loop contend for the same state. Entry group operations from the wrong thread can crash or silently fail.

**Why it happens:** Qt's GUI code runs on the main thread. Avahi's threaded poll runs its own thread. Functions like `avahi_entry_group_reset()` are not thread-safe unless called from within the Avahi thread or with the poll locked.

**How to avoid:** All Avahi entry group operations (add_service, reset, commit) must be called from within Avahi callbacks — triggered via `avahi_threaded_poll_lock()` + function call + `avahi_threaded_poll_unlock()`, or from the Avahi callback context itself.

### Pitfall 3: AirPlay Features Bitfield Stale or Wrong

**What goes wrong:** The receiver appears in the AirPlay menu but connecting fails or the receiver shows with wrong capabilities. Modern iOS (17+, 18+) inspects the features bitfield and may reject receivers that advertise incompatible capability combinations.

**Why it happens:** The features bitfield is a reverse-engineered field. Apple changes which bit combinations are required as iOS evolves. A stale value copied from an old blog post may have worked in iOS 15 but fail in iOS 17.

**How to avoid:** Copy the features value directly from UxPlay's current source (`dnssd_set_airplay_features()` in the FDH2/UxPlay repository) rather than hardcoding a number. Monitor UxPlay and shairport-sync issue trackers when Apple releases iOS updates.

**Warning signs:** Receiver appears in AirPlay menu but connection immediately fails. Check UxPlay GitHub issues for "broken after iOS X.Y" reports.

### Pitfall 4: libupnp Binding to Wrong Interface

**What goes wrong:** `UpnpInit2()` binds to the loopback or a VPN interface. SSDP NOTIFY packets are never seen on the LAN. DLNA controllers do not discover the renderer.

**Why it happens:** `UpnpInit2(nullptr, 0)` binds to the first non-loopback interface. On machines with a VPN active (e.g., wireguard, openvpn), this may be the VPN tunnel interface rather than the LAN interface.

**How to avoid:** Enumerate network interfaces, prefer the interface with an RFC1918 address on the same subnet as mDNS. Pass the chosen interface's address string to `UpnpInit2()`. Log which interface was chosen at startup.

### Pitfall 5: Windows Firewall Registration Without UAC Elevation

**What goes wrong:** `CoCreateInstance(__uuidof(NetFwPolicy2))` succeeds but `pRules->Add(pRule)` returns `E_ACCESSDENIED`. The app silently starts without firewall rules. Discovery fails on a default Windows installation.

**Why it happens:** Writing new firewall rules requires elevation. The app is not running as administrator by default.

**How to avoid:** Per D-12/D-13: detect the `E_ACCESSDENIED` return from `Add()` (or use the COM elevation moniker which triggers a UAC prompt). If elevation is denied, display a dialog listing the exact ports and protocols: "To enable discovery, open Windows Defender Firewall and allow inbound UDP 5353, UDP 1900, TCP 7000, TCP 7100, TCP 8009." Store a `firstRunComplete` flag in QSettings; only attempt registration on first run.

### Pitfall 6: RAOP Service Name Format Wrong

**What goes wrong:** iOS does not discover the AirPlay receiver despite mDNS being active.

**Why it happens:** The `_raop._tcp` service name must be formatted as `<MACADDRESS>@<FriendlyName>` where the MAC address uses uppercase hex without colons (e.g., `AABBCCDDEEFF@AirShow`). Using the friendly name alone, or lowercase MAC, causes iOS to ignore the service record.

**How to avoid:** Format the RAOP service name as `AABBCCDDEEFF@ReceiverName` using the same NIC MAC that populates the `deviceid` TXT field. Verify with `dns-sd -B _raop._tcp` on macOS.

### Pitfall 7: Re-Registration Race on Name Change

**What goes wrong:** When the receiver name changes (D-11), the old service record is briefly visible alongside the new one. Sender devices see duplicate entries.

**Why it happens:** Avahi's `avahi_entry_group_reset()` sends NOTIFY(byebye) and `avahi_entry_group_add_service()` sends NOTIFY(alive). If the re-registration completes while the TTL on the old record is still active, the sender's mDNS cache holds both.

**How to avoid:** Call `avahi_entry_group_reset()` first and wait for the `AVAHI_ENTRY_GROUP_UNCOMMITTED` state callback before calling `avahi_entry_group_add_service()` again. libupnp's DLNA re-registration follows the same pattern: `UpnpUnRegisterRootDevice()` then `UpnpRegisterRootDevice()`.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| libavahi-client-dev | DISC-01, DISC-02 (Linux mDNS) | Not installed (available in apt) | 0.8 | Install via apt |
| avahi-daemon (runtime) | DISC-01, DISC-02 (Linux mDNS) | Running | 0.8 | `sudo systemctl start avahi-daemon` |
| libupnp-dev (pupnp) | DISC-03 (DLNA SSDP) | Not installed (available in apt) | 1.14.24 | Install via apt |
| Qt6::Core (QSettings) | DISC-04 (receiver name) | Installed | 6.9.2 | Already linked |
| dns_sd.h + Bonjour runtime | DISC-01, DISC-02 (macOS/Windows mDNS) | macOS built-in; Windows needs bundling | System | Bundle mDNSResponder on Windows |
| netfw.h + ole32 | DISC-05 (Windows Firewall) | Windows SDK (not applicable on Linux dev) | Windows SDK | Graceful fallback per D-13 |
| CMake | Build system | Installed | 3.31.6 | — |
| GTest | Test infrastructure | Installed | 1.17.0 | — |

**Missing dependencies with no fallback:**
- None — all required dev packages are available via apt or are already installed.

**Missing dependencies needing install before building:**
- `libavahi-client-dev` and `libupnp-dev` — not installed on dev machine; one `apt install` command resolves this. Must be added to CMakeLists.txt with `pkg_check_modules`.

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | GTest 1.17.0 + GMock |
| Config file | tests/CMakeLists.txt (existing pattern) |
| Quick run command | `ctest --test-dir build -R test_discovery --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements to Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DISC-01 | `ServiceAdvertiser` registers `_airplay._tcp` without error | unit | `ctest --test-dir build -R DiscoveryManager -x` | No — Wave 0 |
| DISC-01 | `DiscoveryManager` produces correct AirPlay TXT records (all required fields present) | unit | `ctest --test-dir build -R AirPlayTxtRecords -x` | No — Wave 0 |
| DISC-02 | `DiscoveryManager` produces correct Cast TXT records (id, fn, md, ve, ca present) | unit | `ctest --test-dir build -R CastTxtRecords -x` | No — Wave 0 |
| DISC-03 | `UpnpAdvertiser::start()` calls `UpnpInit2` and `UpnpRegisterRootDevice` without error | unit | `ctest --test-dir build -R UpnpAdvertiser -x` | No — Wave 0 |
| DISC-04 | `AppSettings::receiverName()` returns hostname when no value stored; returns saved name after `setReceiverName()` | unit | `ctest --test-dir build -R AppSettings -x` | No — Wave 0 |
| DISC-04 | `DiscoveryManager::rename()` calls `ServiceAdvertiser::rename()` | unit | `ctest --test-dir build -R DiscoveryManagerRename -x` | No — Wave 0 |
| DISC-05 | `WindowsFirewall::registerRules()` returns false (graceful) when not on Windows | unit | `ctest --test-dir build -R WindowsFirewall -x` | No — Wave 0 |
| DISC-06 | `ProtocolHandler` abstract class instantiation (via mock) satisfies interface contract | unit | `ctest --test-dir build -R ProtocolHandler -x` | No — Wave 0 |
| DISC-06 | `ProtocolManager::registerHandler()` + `startAll()` + `stopAll()` lifecycle | unit | `ctest --test-dir build -R ProtocolManager -x` | No — Wave 0 |

Manual verification (cannot be automated in unit tests):
- DISC-01: App appears in iOS AirPlay menu on same LAN
- DISC-02: App appears in Chrome Cast menu / Android Cast menu on same LAN
- DISC-03: App appears in BubbleUPnP or similar DLNA controller on same LAN
- DISC-04: Name change in settings immediately updates device picker entry
- DISC-05: Fresh Windows install with default firewall — discovery works without manual port opening

### Sampling Rate

- **Per task commit:** `ctest --test-dir build -R test_discovery --output-on-failure`
- **Per wave merge:** `ctest --test-dir build --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/test_discovery.cpp` — covers DISC-01 through DISC-06 unit cases
- [ ] `tests/mocks/MockServiceAdvertiser.h` — GMock mock for `ServiceAdvertiser` interface
- [ ] `tests/mocks/MockProtocolHandler.h` — GMock mock for `ProtocolHandler` interface
- [ ] Update `tests/CMakeLists.txt` to add `test_discovery` target with new source files and `libavahi-client`, `libupnp` linkage

---

## CMakeLists.txt Extension Pattern

The existing `CMakeLists.txt` uses `pkg_check_modules()`. Phase 2 extends this pattern:

```cmake
# Add after existing pkg_check_modules block:
pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)
pkg_check_modules(UPNP REQUIRED IMPORTED_TARGET libupnp)

# Add to qt_add_executable sources:
src/protocol/ProtocolHandler.h
src/protocol/ProtocolManager.cpp
src/discovery/ServiceAdvertiser.h
src/discovery/DiscoveryManager.cpp
src/settings/AppSettings.cpp
# Platform-specific backends via target_sources or if(CMAKE_SYSTEM_NAME) blocks:
# src/discovery/AvahiAdvertiser.cpp       (Linux)
# src/discovery/DnsSdAdvertiser.cpp       (macOS)
# src/discovery/BonjourAdvertiser.cpp     (Windows)
# src/discovery/UpnpAdvertiser.cpp        (all)
# src/platform/WindowsFirewall.cpp        (Windows only)

# Add to target_link_libraries:
PkgConfig::AVAHI    # Linux only — wrap in if(UNIX AND NOT APPLE)
PkgConfig::UPNP     # all platforms
Qt6::Core           # already linked — QSettings lives here
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Avahi simple poll (blocking main loop) | Avahi threaded poll | ~2015 | Avahi must not block Qt event loop; threaded poll is the modern default |
| AirPlay features as 32-bit single hex | 64-bit two-hex comma format `0xHHHHHHHH,0xHHHHHHHH` | iOS 11 era | Phase 2 TXT records must use the comma-separated 64-bit format |
| Bonjour SDK v3.0 (2011, Apple Dev) | mDNSResponder open source (Apache 2.0) | 2011 → ongoing | The SDK download is stale; build from source or use the compat layer via libavahi |
| libupnp 1.6.x | libupnp 1.14.x | ~2016 | API is largely the same; 1.14.x has threading fixes and IPv6 support |

**Deprecated/outdated:**
- Bonjour SDK for Windows v3.0: 2011 binary SDK. Still functional but not updateable. The open-source mDNSResponder is the correct long-term approach.
- `avahi_simple_poll`: Blocking; incompatible with a Qt event loop. Use `avahi_threaded_poll`.

---

## Open Questions

1. **AirPlay `pk` TXT field in Phase 2**
   - What we know: `pk` is required in modern iOS discovery. In Phase 2 no crypto is implemented yet.
   - What's unclear: Does iOS refuse to show a receiver with a zero-padded placeholder `pk`, or only refuse to connect?
   - Recommendation: Advertise a valid-length 64-byte hex string (all zeros or a deterministic fake). UxPlay's Phase 2 equivalent advertises a placeholder. If iOS hides the receiver entirely, generate a throwaway Ed25519 key pair at first run and store it in QSettings — this is a small addition that removes ambiguity.

2. **Bonjour Windows bundling approach**
   - What we know: The 2011 Bonjour SDK is available; mDNSResponder source is Apache 2.0. The `libavahi-compat-libdnssd` approach (same API on Linux/macOS/Windows) is what UxPlay uses.
   - What's unclear: Whether to use the compat shim approach (one `dns_sd.h` codebase, three backends) or three entirely separate backends (Avahi / native dns_sd / Bonjour SDK DLL).
   - Recommendation: Use a single `DnsSdAdvertiser.cpp` compiled on all three platforms, with `#ifdef __linux__` only for the daemon-check code. Link against `libavahi-compat-libdnssd` on Linux, the system `dns_sd.framework` on macOS, and the Bonjour SDK `dnssd.dll` on Windows. This minimises code duplication.

3. **DLNA device UUID stability**
   - What we know: The UPnP `UDN` (UUID) in the device description must be stable across restarts to avoid DLNA controllers treating the device as a new renderer on each launch.
   - What's unclear: Is storing the UUID in QSettings sufficient, or does DLNA controller behavior require a hostname-derived UUID?
   - Recommendation: Generate a UUID once at first run using Qt's `QUuid::createUuid()`, store in QSettings. Use this as the UDN. This is the standard approach for DLNA DMR implementations.

---

## Project Constraints (from CLAUDE.md)

- Must be completely free — no freemium, no ads, no license keys
- Must work on Linux, macOS, and Windows from the same codebase
- Local network only — mDNS/Bonjour, no internet required
- Open source (license TBD)
- GSD workflow enforcement: use Edit/Write tools only through GSD commands
- C++17, Qt 6.8 LTS, GStreamer 1.26.x, OpenSSL 3.x, CMake ≥3.28 (from STACK.md / CLAUDE.md)
- All new code in `airshow` namespace (established in Phase 1)
- Forward declarations in headers, implementations in .cpp
- GTest for testing (existing pattern in tests/)
- pkg_check_modules pattern for new library dependencies (established in Phase 1)

---

## Sources

### Primary (HIGH confidence)
- [Avahi client-publish-service example](https://avahi.org/doxygen/html/client-publish-service_8c-example.html) — Avahi API pattern, threading model, TXT record format
- [Qt 6 QSettings documentation](https://doc.qt.io/qt-6/qsettings.html) — NativeFormat behavior on all three platforms
- [Microsoft Learn — INetFwPolicy2](https://learn.microsoft.com/en-us/windows/win32/api/netfw/nn-netfw-inetfwpolicy2) — Windows Firewall COM API
- [openairplay/airplay-spec service_discovery](https://openairplay.github.io/airplay-spec/service_discovery.html) — AirPlay TXT record field definitions
- [UPnP MediaRenderer:1 Device Template](https://upnp.org/specs/av/UPnP-av-MediaRenderer-v1-Device.pdf) — Authoritative DLNA DMR XML structure
- [pupnp/pupnp GitHub](https://github.com/pupnp/pupnp) — libupnp API and sample code

### Secondary (MEDIUM confidence)
- [oakbits.com — Google Cast protocol discovery](https://oakbits.com/google-cast-protocol-discovery-and-connection.html) — Cast TXT record fields (id, md, fn, ve, ca, st, rs, ic) — verified against Cisco and Chromecast implementation docs
- [AirPlay 2 internals — emanuelecozzi.net](https://emanuelecozzi.net/docs/airplay2/discovery/) — AirPlay 2 features bitfield structure, _airplay._tcp vs _raop._tcp difference
- [FDH2/UxPlay GitHub](https://github.com/FDH2/UxPlay) — features bitfield values in use by active AirPlay implementation
- [Microsoft Learn — Adding Firewall Rules](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ics/c-adding-an-outbound-rule) — INetFwPolicy2 C++ pattern
- [olivierlevon/Bonjour (mDNSResponder mirror)](https://github.com/olivierlevon/Bonjour) — Windows mDNSResponder source bundling approach

### Tertiary (LOW confidence — flag for validation)
- [antimof/UxPlay uxplay.cpp](https://github.com/antimof/UxPlay/blob/master/uxplay.cpp) — Features bitfield exact values (reverse-engineered, may change with iOS updates)

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — libavahi-client 0.8 and libupnp 1.14.24 both confirmed available in apt; Qt6 already linked; all APIs documented
- AirPlay TXT records: MEDIUM — Fields documented from community spec; feature bitfield values from UxPlay source (reverse-engineered)
- Cast TXT records: MEDIUM — Fields documented from multiple sources; `ca` value 5 may need empirical verification
- DLNA device XML: HIGH — UPnP forum official spec; pupnp sample code confirms structure
- Windows Firewall: HIGH — Official Microsoft Learn documentation
- Architecture patterns: HIGH — Avahi threaded poll and libupnp patterns from official examples and reference implementations

**Research date:** 2026-03-28
**Valid until:** 2026-06-28 (stable APIs); re-check AirPlay features bitfield if iOS 18.x+ is released before Phase 4
