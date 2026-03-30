# Phase 7: Security & Hardening - Context

**Gathered:** 2026-03-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement security controls for the receiver: device approval prompts before mirroring starts, optional PIN-based pairing, and network restriction to local (RFC1918) IPs only. This phase adds a security layer between incoming protocol connections and session establishment across all three protocol handlers (AirPlay, DLNA, Cast). Settings are persisted via AppSettings and exposed to QML via SettingsBridge.

</domain>

<decisions>
## Implementation Decisions

### Device Approval (SEC-01)
- **D-01:** When a new device attempts to connect, show a QML dialog popup with the device name, protocol type, and Allow/Deny buttons. Block session establishment (no audio/video) until the user explicitly allows.
- **D-02:** Store approved devices by a stable identifier (MAC address for AirPlay, UUID for Cast, IP+UserAgent for DLNA) in AppSettings under `trustedDevices` list. Previously approved devices connect without re-prompting.
- **D-03:** Add a `requireApproval` boolean to AppSettings (default: `true`). When disabled, all local network devices connect without prompting.
- **D-04:** The approval check is implemented as a method on a new `SecurityManager` class that all protocol handlers call before starting a session. This centralizes the security logic rather than duplicating it in each handler.

### PIN Pairing (SEC-02)
- **D-05:** Add `pinEnabled` boolean and `pin` string (4-digit) to AppSettings. When enabled, the PIN is displayed on the receiver's idle screen overlay.
- **D-06:** PIN verification happens in SecurityManager before device approval. The PIN must match before the allow/deny dialog appears (or auto-connects if already trusted).
- **D-07:** For AirPlay, use UxPlay's built-in PIN/pairing mechanism if available. For DLNA and Cast, implement PIN as a custom challenge: the receiver sends a "PIN required" response and the sender must provide the correct PIN in a follow-up message. If the protocol doesn't support a PIN challenge natively, fall back to requiring the device to be pre-approved (trusted list).

### Network Restriction (SEC-03)
- **D-08:** Check incoming connection source IPs against RFC1918 private ranges: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, and link-local `169.254.0.0/16`. Reject connections from any other IP range immediately at the TCP level.
- **D-09:** On Linux, detect and exclude VPN interfaces (tun0, tun1, wg0, etc.) from listener binding. Bind protocol listeners to specific non-VPN interfaces rather than `0.0.0.0` when a VPN is detected. On macOS/Windows, use equivalent interface detection.
- **D-10:** The network filter is implemented in SecurityManager as a static method `isLocalNetwork(QHostAddress)` called by each protocol handler's connection accept path.

### Settings & UI
- **D-11:** Extend `AppSettings` with: `requireApproval` (bool, default true), `pinEnabled` (bool, default false), `pin` (QString, default ""), `trustedDevices` (QStringList).
- **D-12:** Extend `SettingsBridge` to expose security settings to QML: `requireApproval`, `pinEnabled`, `pin`, `trustedDevices`, `clearTrustedDevices()`.
- **D-13:** The approval dialog is a QML overlay that appears on top of the idle/mirroring screen. It blocks input to the underlying content until dismissed.
- **D-14:** The PIN display is a QML overlay on the idle screen showing the 4-digit PIN in large text when `pinEnabled` is true.

### Claude's Discretion
- SecurityManager internal implementation details (singleton vs dependency injection)
- QML dialog styling and layout
- How to handle approval timeout (auto-deny after N seconds or keep waiting)
- Exact VPN interface detection heuristics per platform
- Whether to persist trusted devices as MAC, UUID, or composite key
- Error messages for rejected connections (log-only vs UI notification)

</decisions>

<canonical_refs>
## Canonical References

### Settings Infrastructure
- `src/settings/AppSettings.h` — QSettings wrapper, extend with security settings
- `src/settings/AppSettings.cpp` — Implementation
- `src/ui/SettingsBridge.h` — QML bridge for settings, extend with security properties
- `src/ui/SettingsBridge.cpp` — Implementation

### Protocol Handlers (add security checks)
- `src/protocol/AirPlayHandler.cpp` — Add SecurityManager check before session start
- `src/protocol/DlnaHandler.cpp` — Add SecurityManager check in SOAP action handler
- `src/protocol/CastHandler.cpp` — Add SecurityManager check on TLS connection accept
- `src/protocol/CastSession.cpp` — Add SecurityManager check on CONNECT namespace

### UI
- `qml/main.qml` — Add approval dialog and PIN display overlays
- `src/ui/ConnectionBridge.h` — May need approval signal/slot for QML dialog

### Build System
- `CMakeLists.txt` — Add SecurityManager source files

### Network
- `src/discovery/AvahiAdvertiser.cpp` — Interface enumeration pattern (reusable for VPN detection)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `AppSettings` — QSettings-based persistence, cross-platform
- `SettingsBridge` — QML context property bridge for settings
- `ConnectionBridge` — Can signal QML for approval dialogs
- `AvahiAdvertiser::createServices()` — Interface enumeration via /sys/class/net (Linux)

### Established Patterns
- QML context properties for C++ to QML bridges
- QMetaObject::invokeMethod for cross-thread operations
- ProtocolHandler interface with start/stop lifecycle

### Integration Points
- Each protocol handler's connection accept path needs SecurityManager::checkConnection()
- QML main.qml needs approval dialog and PIN display components
- AppSettings needs new keys for security settings

</code_context>

<specifics>
## Specific Ideas

- The AvahiAdvertiser already enumerates /sys/class/net interfaces — same pattern can detect tun/wg interfaces for VPN filtering
- ConnectionBridge already has setConnected() — add a requestApproval() signal that QML connects to for showing the dialog
- AirPlay has its own pairing mechanism in UxPlay — leverage it rather than reimplementing

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 07-security-hardening*
*Context gathered: 2026-03-30*
