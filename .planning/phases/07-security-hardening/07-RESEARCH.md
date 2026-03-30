# Phase 7: Security & Hardening - Research

**Researched:** 2026-03-28
**Domain:** C++17/Qt6 security layer — device approval, PIN pairing, RFC1918 network filtering
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Device Approval (SEC-01)**
- D-01: Show a QML dialog popup with device name, protocol type, Allow/Deny buttons. Block session establishment until user explicitly allows.
- D-02: Store approved devices by stable identifier (MAC address for AirPlay, UUID for Cast, IP+UserAgent for DLNA) in AppSettings under `trustedDevices` list. Previously approved devices connect without re-prompting.
- D-03: Add `requireApproval` boolean to AppSettings (default: true). When disabled, all local network devices connect without prompting.
- D-04: Approval check is a method on a new `SecurityManager` class that all protocol handlers call before starting a session.

**PIN Pairing (SEC-02)**
- D-05: Add `pinEnabled` boolean and `pin` string (4-digit) to AppSettings. When enabled, PIN is displayed on receiver's idle screen overlay.
- D-06: PIN verification happens in SecurityManager before device approval. The PIN must match before allow/deny dialog appears.
- D-07: For AirPlay, use UxPlay's built-in PIN/pairing mechanism if available. For DLNA and Cast, implement PIN as a custom challenge; fall back to requiring device to be pre-approved if protocol doesn't support PIN natively.

**Network Restriction (SEC-03)**
- D-08: Check incoming IPs against RFC1918 ranges: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, and link-local `169.254.0.0/16`. Reject non-RFC1918 connections at TCP level.
- D-09: On Linux, detect and exclude VPN interfaces (tun0, tun1, wg0, etc.) from listener binding. Bind to specific non-VPN interfaces rather than `0.0.0.0`.
- D-10: Network filter implemented in SecurityManager as static method `isLocalNetwork(QHostAddress)`.

**Settings & UI**
- D-11: Extend `AppSettings` with: `requireApproval` (bool, default true), `pinEnabled` (bool, default false), `pin` (QString, default ""), `trustedDevices` (QStringList).
- D-12: Extend `SettingsBridge` to expose: `requireApproval`, `pinEnabled`, `pin`, `trustedDevices`, `clearTrustedDevices()`.
- D-13: Approval dialog is a QML overlay on top of idle/mirroring screen. Blocks input to underlying content until dismissed.
- D-14: PIN display is a QML overlay on idle screen showing 4-digit PIN in large text when `pinEnabled` is true.

### Claude's Discretion
- SecurityManager internal implementation details (singleton vs dependency injection)
- QML dialog styling and layout
- How to handle approval timeout (auto-deny after N seconds or keep waiting)
- Exact VPN interface detection heuristics per platform
- Whether to persist trusted devices as MAC, UUID, or composite key
- Error messages for rejected connections (log-only vs UI notification)

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SEC-01 | User is prompted to approve or deny incoming connections before mirroring starts | UxPlay `admit` callback, QMetaObject::invokeMethod cross-thread, QML modal overlay via `requestApproval` signal on ConnectionBridge |
| SEC-02 | User can enable PIN-based pairing so only devices with the PIN can connect | UxPlay `raop_set_plist("pin", value)` + `display_pin` callback (confirmed in vendor source); custom challenge approach for DLNA/Cast |
| SEC-03 | Application only listens on local network interfaces (not exposed to internet) | `QHostAddress::isPrivateUse()` (Qt6, confirmed in header); `QNetworkInterface::allInterfaces()` for VPN interface detection |
</phase_requirements>

---

## Summary

Phase 7 adds a `SecurityManager` class that centralizes three security controls: device approval prompts, PIN pairing, and RFC1918 network filtering. All three protocol handlers (AirPlay, DLNA, Cast) call into `SecurityManager` before establishing a session. This is an additive phase — no protocol logic changes, only interception at connection-accept points that already exist in the codebase.

The most technically interesting part is the cross-thread approval flow: protocol callbacks run on non-Qt threads (UxPlay's RAOP thread, libupnp's thread pool, the Qt event loop for Cast). The approval dialog must run on the Qt main thread. The pattern used elsewhere in the codebase (`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`) handles dispatch, but an approval flow also needs a blocking mechanism to pause the callback until the user responds. A `QSemaphore` initialized to 0 — released by the QML dialog result — is the correct cross-thread blocking pattern for this.

UxPlay's built-in PIN mechanism is confirmed present in the vendored source: `raop_set_plist("pin", value)` enables PIN mode, and the `display_pin` callback in `raop_callbacks_t` delivers the PIN string when a client initiates pairing. This is the correct integration path for AirPlay PIN (D-07). For DLNA and Cast there is no protocol-level PIN mechanism, so SecurityManager must block those connections until the device is in the trusted list (or approval is granted) without a PIN challenge.

**Primary recommendation:** Build `SecurityManager` as a QObject with dependency-injected `AppSettings&` and a `requestApproval` signal emitted via the Qt event loop. Protocol handlers call a synchronous `checkConnection()` that blocks on a per-request `QSemaphore` (timeout configurable). QML connects to the `requestApproval` signal to show the dialog; dialog result calls `approveConnection(requestId, bool)` which releases the semaphore.

---

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt6 `QHostAddress` | 6.8+ (6.9.2 on dev machine) | RFC1918 IP range checks | `isPrivateUse()` method confirmed in Qt6 header — covers 10/8, 172.16/12, 192.168/16, fc00::/7, fe80::/10. No manual CIDR arithmetic needed. |
| Qt6 `QNetworkInterface` | 6.8+ | Interface enumeration for VPN detection | `allInterfaces()` returns `InterfaceFlags` including `IsPointToPoint` (VPN tunnel characteristic). Combined with name heuristics (tun, wg, vpn) for D-09. |
| Qt6 `QSemaphore` | 6.8+ | Cross-thread blocking for approval flow | Zero-initialized semaphore blocks calling thread; QML dialog releases it via `acquire(1)` pattern. Simpler than `QWaitCondition + QMutex` for single-use per-request blocking. |
| Qt6 `QSettings` (via `AppSettings`) | 6.8+ | Persisting trusted devices and security settings | Already established pattern in codebase. `QStringList` for trusted device identifiers. |
| UxPlay `raop_set_plist("pin", value)` + `display_pin` callback | 1.73.6 (vendored) | AirPlay built-in PIN pairing | Confirmed in `vendor/uxplay/lib/raop_handlers.h` and `raop.c`. `use_pin=true` triggers `/pair-pin-start` → `/pair-setup-pin` handshake; PIN displayed via callback. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Qt6 `QUuid` | 6.8+ | Stable Cast device identifier generation | Cast devices identified by UUID in CONNECT namespace; store as string in trustedDevices. |
| Qt6 `QTimer` | 6.8+ | Approval timeout (auto-deny) | If implementing timeout on approval dialog — `QTimer::singleShot` fires on Qt thread to call deny path and release semaphore. |
| GLib `g_warning` | GStreamer dep | Logging rejected connections in AirPlay callback context | Already used throughout AirPlayHandler.cpp; consistent with existing logging convention. |

### Installation
No new dependencies required. All libraries are already available via the existing Qt6 and GStreamer setup.

---

## Architecture Patterns

### Recommended Project Structure

```
src/
├── security/
│   ├── SecurityManager.h        # New: central security check + approval signal
│   └── SecurityManager.cpp
├── settings/
│   ├── AppSettings.h            # Extend: requireApproval, pinEnabled, pin, trustedDevices
│   └── AppSettings.cpp
├── ui/
│   ├── ConnectionBridge.h       # Extend: requestApproval signal for QML
│   ├── SettingsBridge.h         # Extend: security properties + clearTrustedDevices()
│   └── SettingsBridge.cpp
├── protocol/
│   ├── AirPlayHandler.cpp       # Modify: onReportClientRequest calls SecurityManager
│   ├── DlnaHandler.cpp          # Modify: handleSoapAction calls SecurityManager
│   ├── CastHandler.cpp          # Modify: onPendingConnection calls SecurityManager
│   └── CastSession.cpp          # Modify: CONNECT namespace check calls SecurityManager
qml/
├── main.qml                     # Add: ApprovalDialog overlay, PIN display overlay
├── ApprovalDialog.qml           # New: modal approval dialog component
└── IdleScreen.qml               # Modify: add PIN display when pinEnabled
```

### Pattern 1: Cross-Thread Approval with QSemaphore

The core challenge is that protocol callbacks run on non-Qt threads and need to block until the user approves or denies. The `QSemaphore` pattern is the correct Qt idiom for this.

```cpp
// Source: Qt6 QSemaphore docs + established QMetaObject::invokeMethod pattern in codebase
struct ApprovalRequest {
    QString     requestId;
    QString     deviceName;
    QString     protocol;
    QString     deviceIdentifier;
    QSemaphore  semaphore{0};   // starts locked; released by approveConnection()
    bool        approved{false};
};

// SecurityManager::checkConnection() — called from protocol callback thread
bool SecurityManager::checkConnection(const QString& deviceName,
                                      const QString& protocol,
                                      const QString& deviceId) {
    // 1. RFC1918 check happens upstream (isLocalNetwork) before this is called.
    // 2. Trusted device check — no prompt needed.
    if (!m_settings.requireApproval() || isTrusted(deviceId)) {
        return true;
    }
    // 3. Build request and emit signal on Qt thread
    auto req = std::make_shared<ApprovalRequest>();
    req->requestId = QUuid::createUuid().toString();
    req->deviceName = deviceName;
    req->protocol = protocol;
    req->deviceIdentifier = deviceId;

    // invokeMethod marshals to Qt event loop thread (same pattern as ConnectionBridge)
    QMetaObject::invokeMethod(this, [this, req]() {
        m_pendingRequests[req->requestId] = req;
        emit requestApproval(req->requestId, req->deviceName, req->protocol);
    }, Qt::QueuedConnection);

    // Block calling thread (protocol callback) — 30s timeout, auto-deny
    bool acquired = req->semaphore.tryAcquire(1, 30000);
    return acquired && req->approved;
}

// Called from QML dialog via Q_INVOKABLE — runs on Qt thread
Q_INVOKABLE void SecurityManager::resolveApproval(const QString& requestId, bool approved) {
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) return;
    auto req = it.value();
    m_pendingRequests.erase(it);
    req->approved = approved;
    if (approved) addTrustedDevice(req->deviceIdentifier);
    req->semaphore.release(1);  // unblocks protocol callback thread
}
```

**Critical thread-safety note:** `m_pendingRequests` (a QHash) is accessed from both the Qt thread (insert in invokeMethod lambda, remove in resolveApproval) and the protocol thread (reads `req->approved` after semaphore). The QHash itself is only touched from the Qt thread — the protocol thread only holds a `shared_ptr` to the request struct and reads `approved` after `tryAcquire` returns. This is safe because `tryAcquire` provides the memory barrier.

### Pattern 2: AirPlay Integration — `onReportClientRequest` + SecurityManager

UxPlay's `report_client_request` callback is the correct intercept point for AirPlay. The `admit` bool pointer controls whether UxPlay proceeds:

```cpp
// Source: vendor/uxplay/lib/raop.h line 99 + AirPlayHandler.cpp existing pattern
void AirPlayHandler::onReportClientRequest(char* deviceid, char* model,
                                           char* devicename, bool* admit) {
    m_currentDeviceName = (devicename && *devicename) ? std::string(devicename) : "Unknown";
    if (!admit) return;

    if (m_securityManager) {
        QString id = QString::fromLatin1(deviceid ? deviceid : "");
        bool allowed = m_securityManager->checkConnection(
            QString::fromStdString(m_currentDeviceName), "AirPlay", id);
        *admit = allowed;
    } else {
        *admit = true;
    }
}
```

`deviceid` is the MAC address string (e.g. `"AA:BB:CC:DD:EE:FF"`) — this is the stable identifier for D-02.

### Pattern 3: AirPlay PIN — `raop_set_plist("pin", value)` + `display_pin` callback

UxPlay has a complete PIN mechanism. When `use_pin = true` (set via `raop_set_plist("pin", <4-digit int>)`), the RAOP server requires `/pair-pin-start` → `/pair-setup-pin` before accepting a mirror. The `display_pin` callback fires when the 4-digit PIN is ready to display. However, there are two distinct PIN modes in UxPlay:

- **Configured PIN** (`raop_set_plist("pin", value)`): A fixed 4-digit PIN you set. The sender must enter this PIN when pairing.
- **Random password** (`random_pw` path): A randomly generated password displayed when a client connects without prior authentication. This is the HTTP Digest auth path (not the SRP pairing path).

For D-05/D-06, the configured PIN path (`raop_set_plist`) is correct: call it with the user's 4-digit PIN when `pinEnabled` is true. The `display_pin` callback provides the PIN string for display in the QML overlay.

```cpp
// Source: vendor/uxplay/lib/raop.c line 764-766
// In AirPlayHandler::start(), after raop_init2():
if (m_securityManager && m_securityManager->isPinEnabled()) {
    int pin = m_securityManager->pin().toInt();
    raop_set_plist(m_raop, "pin", pin);
    // Wire display_pin callback:
    callbacks.display_pin = raop_cb_display_pin;
}
```

```c
// File-scope C trampoline (same pattern as existing trampolines):
static void raop_cb_display_pin(void* cls, char* pin) {
    static_cast<myairshow::AirPlayHandler*>(cls)->onDisplayPin(pin);
}
```

### Pattern 4: RFC1918 Check Using `QHostAddress::isPrivateUse()`

`QHostAddress::isPrivateUse()` is confirmed in `/usr/include/x86_64-linux-gnu/qt6/QtNetwork/qhostaddress.h` (Qt 6.9.2 on dev machine). It covers RFC1918 IPv4 ranges and IPv6 ULA/link-local. This is exactly what D-08 requires, and it is a single method call rather than manual CIDR arithmetic.

```cpp
// Source: Qt6 QHostAddress header (confirmed line 134) + isInSubnet for link-local
static bool SecurityManager::isLocalNetwork(const QHostAddress& addr) {
    if (addr.isLoopback()) return true;   // 127.x, ::1 — allow local testing
    if (addr.isPrivateUse()) return true; // 10/8, 172.16/12, 192.168/16, fc00::/7
    if (addr.isLinkLocal()) return true;  // 169.254/16, fe80::/10
    return false;
}
```

`isLinkLocal()` covers `169.254.0.0/16` (D-08 explicitly lists this range). The combination of `isPrivateUse()` + `isLinkLocal()` covers all four RFC1918/link-local ranges in D-08 with no manual subnet math.

### Pattern 5: VPN Interface Detection for Listener Binding (D-09)

The existing `AvahiAdvertiser::createServices()` already enumerates `/sys/class/net` to get interface indices. That same pattern can detect VPN interfaces by name prefix:

```cpp
// Source: AvahiAdvertiser.cpp lines 208-233 — reuse enumeration pattern
static bool isVpnInterface(const QString& ifName) {
    // Common VPN interface name prefixes across Linux distros:
    return ifName.startsWith("tun")   ||  // OpenVPN (tun0, tun1)
           ifName.startsWith("wg")    ||  // WireGuard (wg0, wg1)
           ifName.startsWith("vpn")   ||  // generic VPN
           ifName.startsWith("ppp")   ||  // PPP/L2TP
           ifName.startsWith("tap");      // OpenVPN TAP mode
}
```

For binding protocol listeners (RAOP port 7000, DLNA port configured by libupnp, Cast port 8009), `QNetworkInterface::allInterfaces()` with flag filtering is preferable to `/sys/class/net` because it works cross-platform:

```cpp
// Source: Qt6 QNetworkInterface header (confirmed allInterfaces(), IsPointToPoint flag)
QList<QHostAddress> SecurityManager::localNetworkAddresses() {
    QList<QHostAddress> result;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp)) continue;
        if (flags & QNetworkInterface::IsLoopBack) continue;
        if (isVpnInterface(iface.name())) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            QHostAddress addr = entry.ip();
            if (addr.isPrivateUse() || addr.isLinkLocal()) {
                result.append(addr);
            }
        }
    }
    return result;
}
```

**Note on listener binding:** CastHandler currently calls `m_server->listen(QHostAddress::Any, 8009)`. Changing to listen on specific addresses requires iterating `localNetworkAddresses()` and calling `listen()` once per address, or accepting `QHostAddress::Any` with IP-level filtering in the accept path. For AirPlay (raop_start_httpd), UxPlay does not expose a bind-address parameter — the IP-level filter in the accept path is the practical approach. For DLNA, libupnp binds to a specified interface (see `UpnpInit2`).

### Pattern 6: DLNA SecurityManager Integration

DLNA connections arrive in `handleSoapAction()` from libupnp's thread pool. The `UpnpActionRequest` contains the client IP address. The check must happen before the action is processed:

```cpp
// Source: DlnaHandler.cpp existing pattern + UpnpGetActionRequest docs
int DlnaHandler::handleSoapAction(const void* event) {
    const auto* req = static_cast<const UpnpActionRequest*>(event);
    // Get client IP from libupnp request
    const char* clientAddr = UpnpGetActionRequest_get_DevUDN_cstr(req);
    // ... (exact API: check UpnpActionRequest struct for IP field)
    if (m_securityManager && !m_securityManager->isLocalNetwork(clientIp)) {
        return UPNP_E_INVALID_ACTION;
    }
    // Existing action dispatch follows...
}
```

The DLNA SecurityManager check is RFC1918-only (network restriction); device approval for DLNA is harder because DLNA has no persistent device ID. The decision (D-02) uses `IP+UserAgent` as the composite key — extract from `UpnpGetActionRequest_get_ExtraHeadersBlock` or use the client IP alone as a fallback.

### Anti-Patterns to Avoid

- **Calling `QSemaphore::acquire()` on the Qt main thread:** The Qt event loop must not block — the dialog result will never fire. SecurityManager must only be called from protocol handler threads (non-main threads). If called from the main thread (e.g., Cast's `onPendingConnection`), use an async signal/slot round-trip instead of a semaphore.
- **Storing approved status in SecurityManager memory only:** Trusted devices must be persisted to AppSettings immediately when approved. A crash between approval and next launch would re-prompt unnecessarily.
- **Calling `raop_set_plist("pin", value)` after `raop_start_httpd()`:** UxPlay processes PIN during pair-setup which occurs before the mirror stream starts. PIN must be configured before `raop_start_httpd()` is called.
- **Using `QHostAddress::Any` check for VPN detection:** `QHostAddress::Any` means "0.0.0.0" — it does not represent a VPN. Use `QNetworkInterface` name heuristics or the `IsPointToPoint` flag.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| RFC1918 CIDR matching | Manual bit-mask arithmetic for 10/8, 172.16/12, 192.168/16 | `QHostAddress::isPrivateUse()` + `isLinkLocal()` | Confirmed in Qt6 header. IPv6 ULA/link-local covered automatically. |
| AirPlay PIN enforcement | Custom RAOP handler modifications | `raop_set_plist("pin", value)` + `display_pin` callback | UxPlay has a complete PIN-pairing handshake (`/pair-pin-start` → `/pair-setup-pin`). Reimplementing this would break AirPlay compatibility. |
| Cross-thread blocking | `sleep()` polling loop or `QEventLoop::exec()` on worker thread | `QSemaphore::tryAcquire(1, timeoutMs)` | QSemaphore is designed for exactly this: block one thread until another releases. `QEventLoop::exec()` on a non-Qt thread is undefined behavior in Qt. |
| Interface name → index mapping | Re-parsing `/sys/class/net` | `QNetworkInterface::allInterfaces()` | Cross-platform, returns flags including `IsPointToPoint`. The Avahi `/sys/class/net` pattern is Linux-only. |

**Key insight:** The security controls here are almost entirely Qt APIs (QHostAddress, QNetworkInterface, QSemaphore, QSettings) plus one UxPlay plist call. The custom logic is minimal: name-prefix VPN detection, trusted-device list lookup, and the QML dialog wiring.

---

## Common Pitfalls

### Pitfall 1: CastHandler Runs on Qt Main Thread — Cannot Use Semaphore Blocking
**What goes wrong:** `CastHandler::onPendingConnection()` is a Qt slot called on the main event loop. Calling `QSemaphore::tryAcquire()` here blocks the event loop — the QML dialog result signal will never be processed, causing a deadlock.
**Why it happens:** Cast uses `QSslServer` (Qt event loop). UxPlay uses its own thread. DLNA uses libupnp's thread pool. Different threading models.
**How to avoid:** For Cast, use an async approval pattern: accept the socket but do not create a `CastSession` until approval is granted. Store the pending socket in a `QPointer<QSslSocket>`. When `resolveApproval(approved=true)` fires on the Qt thread, then create the CastSession. When `approved=false`, close and delete the socket.
**Warning signs:** App freezes when a Cast connection arrives and approval dialog is expected.

### Pitfall 2: `raop_set_plist("pin", value)` Must Be Called Before `raop_start_httpd()`
**What goes wrong:** If `raop_set_plist` is called after `raop_start_httpd`, existing connections are already being processed without PIN enforcement. New connections may or may not pick up the change depending on when their pair-setup occurs.
**Why it happens:** UxPlay reads `raop->use_pin` during the `/pair-setup-pin` HTTP handler which is called early in the AirPlay handshake.
**How to avoid:** In `AirPlayHandler::start()`, call `raop_set_plist("pin", ...)` between `raop_init2()` and `raop_start_httpd()`.

### Pitfall 3: DLNA `handleSoapAction` — No Persistent Device Identity
**What goes wrong:** DLNA has no device UUID in the AVTransport SOAP actions. Using only IP as device identity means devices behind NAT or with DHCP renewal look like new devices.
**Why it happens:** DLNA/UPnP does not mandate device identity in action requests (unlike AirPlay MAC or Cast UUID).
**How to avoid:** Per D-02, use `IP+UserAgent` composite key. Extract from `UpnpGetActionRequest` headers. Accept that this is imperfect and document it. For approval purposes, it is "good enough" for a local network receiver.

### Pitfall 4: `QStringList` for TrustedDevices Has O(n) Lookup
**What goes wrong:** Every connection check scans the entire trusted list. On a large list this is slow; on a typical home network (5-20 devices) it is fine.
**Why it happens:** QSettings stores QStringList as a flat list with no indexing.
**How to avoid:** Load `trustedDevices` into a `QSet<QString>` in SecurityManager's constructor and sync with AppSettings on each add/remove. The QSet is in-memory; AppSettings is the persistent store.

### Pitfall 5: AirPlay `onReportClientRequest` Callback Has No Return Path for "Wait"
**What goes wrong:** The `admit` bool must be set synchronously — UxPlay's RAOP server does not have an async "please wait" response for the client. If the semaphore blocks for too long (>10-15s), the iOS/macOS device will time out and show a connection error.
**Why it happens:** UxPlay's HTTP handler is synchronous. The `report_client_request` callback must return quickly from UxPlay's perspective.
**How to avoid:** Use `tryAcquire(1, timeoutMs)` with a timeout of 15000ms (15 seconds) — long enough for a human to respond, short enough to avoid iOS timeout. If `tryAcquire` returns false (timeout), set `*admit = false` (auto-deny).

### Pitfall 6: `isPrivateUse()` Returns False for `::1` (IPv6 Loopback)
**What goes wrong:** `QHostAddress("::1").isPrivateUse()` returns false. `QHostAddress("::1").isLoopback()` returns true. If the check is `isPrivateUse()` only, local connections from the same machine are rejected.
**Why it happens:** IPv6 loopback is not a private-use range per IANA.
**How to avoid:** The `isLocalNetwork()` static method (D-10) must check `isLoopback() || isPrivateUse() || isLinkLocal()` — three checks, not one.

---

## Code Examples

### SecurityManager header sketch

```cpp
// Source: established project patterns (ConnectionBridge, SettingsBridge) + Qt6 QSemaphore
class SecurityManager : public QObject {
    Q_OBJECT
public:
    explicit SecurityManager(AppSettings& settings, QObject* parent = nullptr);

    // Called from protocol callback threads (not Qt main thread for AirPlay/DLNA).
    // Blocks until user approves/denies or timeout expires.
    // For Cast (Qt main thread), use checkConnectionAsync() + pendingSocket pattern.
    bool checkConnection(const QString& deviceName,
                         const QString& protocol,
                         const QString& deviceIdentifier);

    // Non-blocking; emits requestApproval. For use from Qt main thread (Cast).
    void checkConnectionAsync(const QString& deviceName,
                              const QString& protocol,
                              const QString& deviceIdentifier,
                              std::function<void(bool)> callback);

    // RFC1918 check — called before checkConnection in each handler's accept path.
    static bool isLocalNetwork(const QHostAddress& addr);

    // Returns addresses on non-VPN LAN interfaces.
    static QList<QHostAddress> localNetworkAddresses();

    bool isPinEnabled() const;
    QString pin() const;

signals:
    // Emitted on Qt thread — QML connects to show dialog.
    void requestApproval(const QString& requestId,
                         const QString& deviceName,
                         const QString& protocol);

public slots:
    // Called by QML dialog result. Runs on Qt thread.
    Q_INVOKABLE void resolveApproval(const QString& requestId, bool approved);
};
```

### AppSettings extension

```cpp
// New methods to add to AppSettings (QSettings key names):
bool requireApproval() const;       // key: "security/requireApproval", default true
void setRequireApproval(bool v);
bool pinEnabled() const;            // key: "security/pinEnabled", default false
void setPinEnabled(bool v);
QString pin() const;                // key: "security/pin", default ""
void setPin(const QString& v);
QStringList trustedDevices() const; // key: "security/trustedDevices", default {}
void addTrustedDevice(const QString& id);
void clearTrustedDevices();
```

### QML ApprovalDialog overlay

```qml
// Source: established QML overlay pattern from HudOverlay.qml + Qt Popup/Rectangle approach
Item {
    id: approvalDialog
    anchors.fill: parent
    visible: opacity > 0
    opacity: connectionBridge.approvalPending ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 150 } }

    // Block input to underlying content (D-13)
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Rectangle {
        anchors.centerIn: parent
        width: 420; height: 200
        color: "#CC1A1A2E"; radius: 12

        Column {
            anchors.centerIn: parent; spacing: 20

            Text {
                text: "Allow " + connectionBridge.pendingDeviceName +
                      " (" + connectionBridge.pendingProtocol + ") to connect?"
                color: "white"; font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
            }
            Row {
                spacing: 24; anchors.horizontalCenter: parent.horizontalCenter
                // Allow and Deny buttons wired to securityManager.resolveApproval()
            }
        }
    }
}
```

### UxPlay PIN wiring

```cpp
// Source: vendor/uxplay/lib/raop.c line 764-766, raop_handlers.h line 270
// In AirPlayHandler::start(), between raop_init2() and raop_start_httpd():
if (m_securityManager && m_securityManager->isPinEnabled()) {
    int pinValue = m_securityManager->pin().toInt();
    raop_set_plist(m_raop, "pin", pinValue);
    callbacks.display_pin = raop_cb_display_pin; // file-scope C trampoline
}
```

```c
// File-scope C trampoline (add to AirPlayHandler.cpp alongside existing trampolines):
static void raop_cb_display_pin(void* cls, char* pin) {
    static_cast<myairshow::AirPlayHandler*>(cls)->onDisplayPin(pin ? pin : "");
}
```

```cpp
// AirPlayHandler::onDisplayPin — marshal to Qt thread for QML display
void AirPlayHandler::onDisplayPin(const std::string& pin) {
    if (m_connectionBridge) {
        QString p = QString::fromStdString(pin);
        QMetaObject::invokeMethod(m_connectionBridge, [this, p]() {
            // ConnectionBridge emits signal that QML IdleScreen reads
            m_connectionBridge->setDisplayPin(p);
        }, Qt::QueuedConnection);
    }
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual CIDR bit-masking for RFC1918 | `QHostAddress::isPrivateUse()` | Qt 5.11+ | Single method call, covers IPv4 + IPv6 ULA |
| Polling `/sys/class/net` directly | `QNetworkInterface::allInterfaces()` | Qt 4.4+ | Cross-platform, flag-based VPN detection |
| UxPlay `random_pw` digest auth | UxPlay `use_pin` + SRP pair-setup-pin | UxPlay 1.x | SRP is the modern AirPlay 2 pairing method |

---

## Open Questions

1. **Cast device identifier for trusted list**
   - What we know: Cast CONNECT message contains `senderId` and `sourceId` in the CASTV2 namespace. CastSession processes the CONNECT namespace.
   - What's unclear: Whether `senderId` or a stable device UUID is present in the Cast CONNECT message. Needs verification by reading CastSession.cpp's CONNECT handling.
   - Recommendation: Read `CastSession.cpp` CONNECT namespace handler during planning/implementation. If no stable UUID is present, use TLS client certificate fingerprint (from `QSslSocket::peerCertificate()`) as a stable identifier — self-signed certs are stable per device.

2. **libupnp client IP extraction**
   - What we know: `handleSoapAction` receives a `UpnpActionRequest*` (cast from `void*`). libupnp provides accessor macros for request fields.
   - What's unclear: Exact macro/field name for client IP in `UpnpActionRequest`. The libupnp headers are vendored to `/tmp`.
   - Recommendation: Check `vendor/` or `/tmp/libupnp-dev` headers for `UpnpActionRequest_get_CtrlPtIPAddr` or equivalent during Wave 0.

3. **Cast async approval — socket lifetime**
   - What we know: CastHandler::onPendingConnection() runs on Qt main thread. Async approval means holding the QSslSocket in a pending state.
   - What's unclear: Whether QSslServer will close the pending socket if not immediately consumed, or if it stays open indefinitely.
   - Recommendation: Call `m_server->nextPendingConnection()` immediately (to take ownership), but don't create the CastSession until approval. Hold the socket as a `QPointer<QSslSocket>`. If `resolveApproval(false)`, call `socket->disconnectFromHost()`.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt6::Network (`QHostAddress`, `QNetworkInterface`, `QSslServer`) | All security checks + Cast | Yes | 6.9.2 | — |
| Qt6::Core (`QSemaphore`, `QSettings`, `QUuid`) | Approval blocking + persistence | Yes | 6.9.2 | — |
| UxPlay `raop_set_plist("pin", ...)` + `display_pin` callback | AirPlay PIN (SEC-02) | Yes | 1.73.6 (vendored) | — |
| GTest (for test_security target) | Unit tests | Yes (used by all existing tests) | System | — |

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest (GTest + GMock) |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `ctest -R test_security --output-on-failure` |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| SEC-01 | `SecurityManager::checkConnection()` returns true for trusted device without prompting | unit | `ctest -R test_security -k "TrustedDeviceSkipsPrompt"` | Wave 0 |
| SEC-01 | `checkConnection()` returns false when denied (semaphore released with false) | unit | `ctest -R test_security -k "DeniedConnectionReturnsFalse"` | Wave 0 |
| SEC-01 | `checkConnection()` returns false on timeout (semaphore not released within window) | unit | `ctest -R test_security -k "TimeoutAutoDenies"` | Wave 0 |
| SEC-01 | `requireApproval=false` skips prompt and returns true | unit | `ctest -R test_security -k "RequireApprovalFalseSkipsPrompt"` | Wave 0 |
| SEC-02 | `AppSettings::pinEnabled()` round-trips through QSettings | unit | `ctest -R test_security -k "PinSettingsRoundTrip"` | Wave 0 |
| SEC-02 | `AppSettings::pin()` stores and retrieves 4-digit string | unit | `ctest -R test_security -k "PinValueRoundTrip"` | Wave 0 |
| SEC-03 | `isLocalNetwork(10.x.x.x)` returns true | unit | `ctest -R test_security -k "RFC1918TenNet"` | Wave 0 |
| SEC-03 | `isLocalNetwork(172.16.x.x)` returns true | unit | `ctest -R test_security -k "RFC1918OneSevenTwo"` | Wave 0 |
| SEC-03 | `isLocalNetwork(192.168.x.x)` returns true | unit | `ctest -R test_security -k "RFC1918OneNineTwo"` | Wave 0 |
| SEC-03 | `isLocalNetwork(169.254.x.x)` returns true (link-local) | unit | `ctest -R test_security -k "LinkLocal"` | Wave 0 |
| SEC-03 | `isLocalNetwork(8.8.8.8)` returns false (public IP) | unit | `ctest -R test_security -k "PublicIPRejected"` | Wave 0 |
| SEC-03 | `isLocalNetwork(127.0.0.1)` returns true (loopback) | unit | `ctest -R test_security -k "LoopbackAllowed"` | Wave 0 |
| SEC-01 | `addTrustedDevice()` persists across AppSettings instances | unit | `ctest -R test_security -k "TrustedDevicePersists"` | Wave 0 |
| SEC-01 | `clearTrustedDevices()` removes all entries | unit | `ctest -R test_security -k "ClearTrustedDevices"` | Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest -R test_security --output-on-failure`
- **Per wave merge:** `ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/test_security.cpp` — covers all SEC-01, SEC-02, SEC-03 unit tests above
- [ ] Add `test_security` target to `tests/CMakeLists.txt` with `AppSettings.cpp`, `SecurityManager.cpp`, `Qt6::Core`, `Qt6::Network`

---

## Sources

### Primary (HIGH confidence)
- `vendor/uxplay/lib/raop.h` line 99-100 — `report_client_request` + `display_pin` callback signatures confirmed in vendored source
- `vendor/uxplay/lib/raop.c` lines 764-766 — `raop_set_plist("pin", value)` sets `use_pin=true`
- `vendor/uxplay/lib/raop_handlers.h` lines 270-271, 681-682 — `display_pin` callback invocation confirmed
- `/usr/include/x86_64-linux-gnu/qt6/QtNetwork/qhostaddress.h` line 134 — `isPrivateUse()` confirmed (Qt 6.9.2)
- `/usr/include/x86_64-linux-gnu/qt6/QtNetwork/qhostaddress.h` lines 124-129 — `isInSubnet()`, `isLoopback()`, `isLinkLocal()` confirmed
- `/usr/include/x86_64-linux-gnu/qt6/QtNetwork/qnetworkinterface.h` lines 78-132 — `allInterfaces()`, `IsLoopBack`, `IsPointToPoint` flags confirmed
- `src/protocol/AirPlayHandler.cpp` — existing trampoline pattern for C callbacks
- `src/ui/ConnectionBridge.h` — existing QObject bridge pattern for QML
- `src/settings/AppSettings.cpp` — QSettings key/value pattern to extend

### Secondary (MEDIUM confidence)
- Qt6 QSemaphore documentation — `tryAcquire(n, timeout)` API for cross-thread blocking
- QMetaObject::invokeMethod pattern — established in existing codebase for cross-thread Qt calls

### Tertiary (LOW confidence)
- libupnp `UpnpActionRequest` client IP accessor — exact macro name unverified; needs checking in vendored headers at `/tmp/libupnp-dev`

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all APIs verified in vendored/system headers
- Architecture: HIGH — patterns derived directly from existing codebase conventions
- Pitfalls: HIGH — derived from reading actual UxPlay source and Qt header inspection
- Validation: HIGH — follows established test_* target pattern

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (Qt APIs are stable LTS; UxPlay vendored, no drift risk)
