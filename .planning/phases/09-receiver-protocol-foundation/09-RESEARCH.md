# Phase 9: Receiver Protocol Foundation - Research

**Researched:** 2026-03-31
**Domain:** Custom TCP protocol handler (C++/Qt), mDNS service advertisement (Avahi), Flutter monorepo setup
**Confidence:** HIGH

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RECV-01 | AirShow receiver accepts connections from companion sender app via custom AirShow protocol on port 7400 | QTcpServer/QTcpSocket pattern confirmed; MiracastHandler is a direct template; 16-byte binary framing from STATE.md decisions |
| RECV-02 | AirShow receiver advertises `_airshow._tcp` via mDNS so sender apps discover it automatically | DiscoveryManager.cpp already calls `m_advertiser->advertise()` for 4 service types; adding a 5th is one line; AvahiAdvertiser handles multi-group correctly |
| RECV-03 | Protocol handshake includes quality negotiation (resolution, bitrate, latency mode) | JSON over QTcpSocket (same pattern as CastSession); QJsonDocument already used in codebase; handshake fields defined by STATE.md decisions |
</phase_requirements>

---

## Summary

Phase 9 builds the receiver side of the custom AirShow protocol that will be consumed by the Flutter companion sender app in Phases 10-13. It has three components: (1) `AirShowHandler` — a new `ProtocolHandler` subclass that listens on TCP port 7400, performs a JSON handshake with quality negotiation, then feeds NAL units from the established connection into the existing `MediaPipeline::videoAppsrc()`; (2) `_airshow._tcp` mDNS advertisement wired into the existing `DiscoveryManager`; and (3) a `sender/` Flutter project directory scaffolded in the repo alongside `src/` so Flutter and C++ development can proceed in parallel.

All three receiver-side components are straightforward additions to an already mature codebase. `AirShowHandler` follows the `MiracastHandler` pattern exactly — `QObject + ProtocolHandler`, `QTcpServer` listening on a fixed port, `QTcpSocket` for the session, `readyRead` signal driving a state machine. The mDNS advertisement is a single `advertise()` call added to `DiscoveryManager::start()`. The Flutter scaffold requires Flutter to be installed and `flutter create` to be run; a Wave 0 task must gate on this.

**Primary recommendation:** Model `AirShowHandler` directly on `MiracastHandler` (TCP server, Qt event loop, no manual threading). Use `QJsonDocument` for handshake encoding (already used in `CastSession`). Use 16-byte binary framing for the streaming data channel per the locked STATE.md decision. Add `_airshow._tcp` in `DiscoveryManager::start()` alongside existing service types.

---

## User Constraints (from STATE.md Decisions)

> No formal CONTEXT.md exists for this phase — constraints come from `STATE.md ## Decisions` which represent locked v2.0 stack decisions.

### Locked Decisions

- **v2.0 stack:** Flutter 3.41.5 for sender app (only cross-platform option covering all 5 targets); receiver stack unchanged (C++17 + Qt 6.8 + GStreamer)
- **Protocol transport:** Custom 16-byte binary TCP framing: `type (1B) | flags (1B) | length (4B) | PTS (8B)` — no third-party transport library
- **Native-handles-media rule:** Dart controls only session state; native plugin handles capture + encode + socket send (MethodChannel too slow for 30fps frame data)
- **Phase order:** Receiver first (hard dependency), then Android, iOS, macOS, Windows, web interface last
- **mDNS package (sender side):** `multicast_dns` 0.3.3 (flutter.dev) — covers all 5 platforms
- **Port 7400** for AirShow protocol; port 7401 for local web interface

### Claude's Discretion

- JSON structure of the handshake (field names, schema version)
- Whether to scope `AirShowHandler` to a single active session or allow queueing (prior handlers all use single-session)
- How many plans to split Phase 9 into (prior phases used 3 plans each)

### Deferred Ideas (OUT OF SCOPE for Phase 9)

- Flutter sender implementation (Phases 10-13)
- Quality negotiation algorithm on the sender side
- QR code / manual IP fallback (Phase 11/14)
- Web interface (Phase 14)

---

## Standard Stack

### Core (all already in the project — no new dependencies)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt6::Network | 6.9.2 (confirmed on machine) | `QTcpServer`, `QTcpSocket` | Already linked in `CMakeLists.txt` via `target_link_libraries(airshow … Qt6::Network)`. Same classes used by `MiracastHandler` and `CastHandler`. |
| Qt6::Core `QJsonDocument` | 6.9.2 | JSON handshake encoding/decoding | Already used in `CastSession.cpp`. No external JSON library needed. |
| Avahi (AvahiAdvertiser) | 0.8 (confirmed on machine) | `_airshow._tcp` mDNS advertisement | `DiscoveryManager` already calls `m_advertiser->advertise()` for 4 service types. Adding a 5th is trivial. |
| GStreamer appsrc | 1.26.x (confirmed via pkg-config ≥1.20) | Feed NAL units into video pipeline | `MediaPipeline::videoAppsrc()` already exists and is used by AirPlay and Miracast handlers. |

### Supporting (Phase 9 Flutter scaffold)

| Tool | Version | Purpose | When to Use |
|------|---------|---------|-------------|
| Flutter SDK | 3.41.5 (locked in STATE.md) | `flutter create sender/` scaffold + `flutter analyze` | Wave 0: scaffold creation. Not installed on this machine — Wave 0 plan must include install step. |

### No New C++ Dependencies

Phase 9 adds zero new C++ library dependencies. All required capabilities (`QTcpServer`, `QJsonDocument`, `GstElement* videoAppsrc()`, `AvahiAdvertiser::advertise()`) are already present in the build system and codebase.

**Version verification (confirmed 2026-03-31):**
- Qt6: `pkg-config --modversion Qt6Network` → `6.9.2`
- Avahi: `pkg-config --modversion avahi-client` → `0.8`
- GTest: `pkg-config --modversion gtest` → `1.17.0`
- Port 7400: `ss -tlnp | grep 7400` → not in use

---

## Architecture Patterns

### Recommended Project Structure (additions only)

```
src/
├── protocol/
│   ├── AirShowHandler.h       # new — QObject + ProtocolHandler, TCP 7400
│   ├── AirShowHandler.cpp     # new
│   └── ... (existing handlers)
├── discovery/
│   └── DiscoveryManager.cpp   # modified — add _airshow._tcp to start()
tests/
│   └── test_airshow.cpp       # new — Wave 0 stubs
sender/                         # new — Flutter project root (Success Criterion 4)
│   ├── pubspec.yaml
│   ├── lib/
│   │   └── main.dart
│   ├── android/
│   ├── ios/
│   ├── macos/
│   └── windows/
```

### Pattern 1: AirShowHandler — TCP Server (from MiracastHandler)

**What:** `QObject` + `ProtocolHandler` subclass. `QTcpServer` listens on port 7400. `newConnection` signal triggers `onConnection()`. Per-socket state machine drives handshake → streaming phases. All I/O on Qt event loop — no manual threading.

**When to use:** Exactly this pattern. It matches every existing TCP-based handler in the project.

**Example (from `MiracastHandler.cpp`, adapted for AirShowHandler):**
```cpp
// Source: src/protocol/MiracastHandler.cpp (established codebase pattern)
bool AirShowHandler::start() {
    if (m_running) return true;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &AirShowHandler::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, kAirShowPort)) {
        qCritical("AirShowHandler: failed to listen on port %u: %s",
                  static_cast<unsigned>(kAirShowPort),
                  qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_running = true;
    return true;
}
```

### Pattern 2: JSON Handshake with QJsonDocument (from CastSession)

**What:** Sender connects → sends a JSON `HELLO` object → receiver reads it, validates, constructs a JSON `HELLO_ACK` with accepted quality parameters → sends back. After ACK, session enters streaming phase.

**Handshake schema (locked fields per RECV-03):**

Sender HELLO (sent by Flutter app):
```json
{
  "type": "HELLO",
  "version": 1,
  "deviceName": "Pixel 9 Pro",
  "codec": "h264",
  "maxResolution": "1920x1080",
  "targetBitrate": 4000000,
  "fps": 30
}
```

Receiver HELLO_ACK (echoed back with accepted values per Success Criterion 3):
```json
{
  "type": "HELLO_ACK",
  "version": 1,
  "codec": "h264",
  "acceptedResolution": "1920x1080",
  "acceptedBitrate": 4000000,
  "acceptedFps": 30
}
```

**Example (from CastSession.cpp pattern):**
```cpp
// Source: src/protocol/CastSession.cpp (established pattern)
void AirShowHandler::onHandshakeData() {
    QByteArray data = m_socket->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        // invalid handshake — disconnect
        m_socket->disconnectFromHost();
        return;
    }
    QJsonObject hello = doc.object();
    // ... validate and construct ACK
    QJsonObject ack;
    ack["type"]               = "HELLO_ACK";
    ack["version"]            = 1;
    ack["codec"]              = "h264";
    ack["acceptedResolution"] = hello["maxResolution"];
    ack["acceptedBitrate"]    = hello["targetBitrate"];
    ack["acceptedFps"]        = hello["fps"];
    m_socket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
    m_state = State::Streaming;
}
```

### Pattern 3: 16-Byte Binary Frame Header (locked in STATE.md)

**What:** After handshake completes, the sender sends a stream of binary frames. Each frame is prefixed with a 16-byte header.

**Header layout (from STATE.md locked decision):**

```
Offset  Size  Field
  0      1B   type  (e.g., 0x01=VIDEO_NAL, 0x02=AUDIO, 0x03=KEEPALIVE)
  1      1B   flags (e.g., 0x01=keyframe, 0x02=end-of-access-unit)
  2      4B   length  (big-endian uint32 — payload byte count, NOT including header)
  6      8B   PTS     (big-endian int64 — nanoseconds, same unit as GstClockTime)
 14      2B   (reserved — zero)
 16      --   payload (length bytes)
```

**Reading pattern (QTcpSocket accumulator):**
```cpp
// Source: established QTcpSocket framing pattern (Qt docs)
void AirShowHandler::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    while (m_readBuffer.size() >= kFrameHeaderSize) {
        quint32 payloadLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_readBuffer.constData()) + 2);
        if (m_readBuffer.size() < kFrameHeaderSize + static_cast<int>(payloadLen))
            break;  // partial frame — wait for more data
        // parse header and dispatch payload
        processFrame(m_readBuffer.left(kFrameHeaderSize + payloadLen));
        m_readBuffer.remove(0, kFrameHeaderSize + payloadLen);
    }
}
```

### Pattern 4: NAL Unit Injection via videoAppsrc (from AirPlayHandler)

**What:** After parsing a VIDEO_NAL frame, wrap the payload in a `GstBuffer` with correct PTS and push via `gst_app_src_push_buffer()` into `MediaPipeline::videoAppsrc()`.

**Example (from AirPlayHandler.cpp):**
```cpp
// Source: src/protocol/AirPlayHandler.cpp (established appsrc pattern)
GstBuffer* buf = gst_buffer_new_allocate(nullptr, payloadSize, nullptr);
gst_buffer_fill(buf, 0, payloadData, payloadSize);
GST_BUFFER_PTS(buf) = ptsNs;
GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
gst_app_src_push_buffer(GST_APP_SRC(m_pipeline->videoAppsrc()), buf);
```

### Pattern 5: _airshow._tcp mDNS Advertisement (from DiscoveryManager)

**What:** Add one `m_advertiser->advertise()` call inside `DiscoveryManager::start()` — same structure as the four existing services.

**Example (from DiscoveryManager.cpp pattern):**
```cpp
// Source: src/discovery/DiscoveryManager.cpp — add after _display._tcp block
static constexpr uint16_t kAirShowPort = 7400;
std::vector<TxtRecord> airshowTxt = {
    {"ver", "1"},           // protocol version
    {"fn",  name},          // friendly name (matches other services)
};
m_advertiser->advertise("_airshow._tcp", name, kAirShowPort, airshowTxt);
```

### Pattern 6: Flutter Scaffold (new — no existing codebase pattern)

**What:** `flutter create` run in the repo root creates `sender/` as a standard Flutter app. The project targets Android, iOS, macOS, and Windows from the start.

**Command:**
```bash
# Run from repo root
flutter create --org com.airshow --project-name airshow_sender sender
```

This creates:
```
sender/
├── pubspec.yaml
├── lib/main.dart         # placeholder screen (Success Criterion 4)
├── android/
├── ios/
├── macos/
├── windows/
└── test/
```

**Verification:**
```bash
cd sender && flutter analyze
```
Must exit 0 with no errors on a fresh scaffold.

### Anti-Patterns to Avoid

- **Separate GStreamer pipeline for AirShow:** AirShowHandler must reuse `MediaPipeline::initAppsrcPipeline()` and `videoAppsrc()` — same as AirPlay and Miracast. Never create a second pipeline.
- **Blocking reads on Qt event loop:** Never call `QTcpSocket::waitForReadyRead()` — connect to `readyRead` signal instead. All prior handlers do this correctly.
- **Framing by newline or delimiter:** The data channel uses binary framing with explicit length fields. Do not use `QTextStream` or line-oriented reads for the streaming phase.
- **Multi-session architecture:** All prior handlers are single-session (new connection replaces old). AirShowHandler must follow the same pattern.
- **Adding `_airshow._tcp` as a separate advertiser:** It must be part of `DiscoveryManager::start()` — not a standalone Avahi client — to benefit from the existing collision-handling and rename() logic.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| TCP server accept loop | Manual `accept()` / thread pool | `QTcpServer` + `newConnection` signal | Qt handles accept, backlog, and OS-level socket management. Already used in MiracastHandler and CastHandler. |
| JSON encoding/decoding | Custom serializer | `QJsonDocument` / `QJsonObject` | Already imported in `CastSession.cpp`. Handles escaping, Unicode, and parse errors correctly. |
| mDNS advertisement | Direct Avahi API calls | `DiscoveryManager::start()` addition | `AvahiAdvertiser` already handles thread locking, entry group lifecycle, name collision, and rename. |
| Buffer accumulation | `std::string` append | `QByteArray` + `remove(0, n)` | `QByteArray` has O(1) front removal via `remove()` and integrates with `QTcpSocket::readAll()`. Already used in `MiracastHandler`. |
| H.264 decode pipeline | New GStreamer pipeline | `MediaPipeline::initAppsrcPipeline()` + `videoAppsrc()` | The appsrc pipeline is already built, tested, and hardware-decode-capable from Phase 4. Reuse it. |

**Key insight:** Phase 9 is almost entirely integration work — wiring existing subsystems (`QTcpServer`, `AvahiAdvertiser`, `MediaPipeline`) together in a new handler. There is nothing fundamentally new to build on the C++ side.

---

## Common Pitfalls

### Pitfall 1: Port 7400 is the RTPS/DDS Default Discovery Port

**What goes wrong:** RTPS (Real-Time Publish-Subscribe) Data Distribution Service — used in robotics (ROS 2), defense, and industrial IoT — assigns port 7400 as its default participant discovery port. A machine running ROS 2 or RTI Connext DDS will already have port 7400 in use.

**Why it happens:** IANA lists port 7400 as `rtps-disc` (RTPS/DDS Discovery, UDP/TCP). It is not in `/etc/services` by default on Ubuntu but is a well-known conflict in ROS 2 environments.

**How to avoid:** Make port 7400 a configurable setting (with 7400 as default) that users or installers can override. Document the conflict prominently in installation notes. The planner should add a `kAirShowPort` constant (not a hardcoded literal) and wire it through `AppSettings`.

**Warning signs:** `QTcpServer::listen()` returns false with `QAbstractSocket::AddressAlreadyInUse` on machines with ROS 2 or DDS middleware installed.

### Pitfall 2: Partial JSON at Handshake Phase

**What goes wrong:** The sender's HELLO JSON may arrive in two `readyRead` signals (TCP fragmentation). Calling `QJsonDocument::fromJson(socket->readAll())` on a partial payload produces a null document, causing the handler to disconnect the client.

**Why it happens:** TCP is stream-oriented. Small JSON payloads (< MTU) rarely fragment, but it can happen — especially on loopback in tests.

**How to avoid:** Accumulate into a `QByteArray` buffer. Look for either a newline terminator or a length-prefix before attempting to parse. For the handshake, the simplest robust approach is to length-prefix the JSON with a 4-byte big-endian size (same approach as many protocol implementations). Alternatively, read until `\n` and require newline-terminated JSON — simpler for the handshake phase only.

**Warning signs:** Intermittent test failures where `onHandshakeData()` sees a null `QJsonDocument` on the first invocation but succeeds on retry.

### Pitfall 3: initAppsrcPipeline() Already Called by Another Handler

**What goes wrong:** `MediaPipeline::initAppsrcPipeline()` is called by the first handler that gets a connection (AirPlay in v1). If AirShowHandler also calls it on connection, and an AirPlay session is already active, the pipeline may be reconfigured or fail to initialize.

**Why it happens:** `MediaPipeline` manages a single appsrc pipeline. Multiple handlers share it via `videoAppsrc()` / `audioAppsrc()` pointers.

**How to avoid:** `AirShowHandler` must check `m_pipeline->videoAppsrc() != nullptr` before calling `initAppsrcPipeline()`. Call `initAppsrcPipeline()` only if the pipeline has not yet been initialized. This is the same pattern that AirPlay and Miracast handlers use — they call `initAppsrcPipeline()` in their `onConnection` callback only when the pipeline is not already in the appsrc state.

**Warning signs:** Second handler connection silently produces no video because `videoAppsrc()` returns `nullptr` or the pipeline is in a wrong state.

### Pitfall 4: DiscoveryManager Rename Does Not Include _airshow._tcp If Added Separately

**What goes wrong:** If `_airshow._tcp` is added to `DiscoveryManager::start()` but the `rename()` path does not re-register it, renaming the receiver causes AirPlay/Cast/Miracast names to update but AirShow name to stay stale.

**Why it happens:** `AvahiAdvertiser::rename()` re-registers ALL services registered via `advertise()` — as long as they all went through the same `AvahiAdvertiser` instance. This is automatic if `_airshow._tcp` is added via `DiscoveryManager::start()`. It breaks only if a separate advertiser is mistakenly created.

**How to avoid:** Always add the new service to the existing `DiscoveryManager::start()` block. Never instantiate a second `AvahiAdvertiser`.

### Pitfall 5: Flutter SDK Not Installed on This Machine

**What goes wrong:** Wave 0 plan includes `flutter create sender/` but Flutter is not in PATH. The task silently fails or blocks.

**Why it happens:** Flutter is not installed on this development machine (`which flutter` → not found; `snap list` → flutter snap not installed).

**How to avoid:** Wave 0 must include a Flutter SDK installation step (snap install flutter --classic or SDK download). The planner should make Flutter installation an explicit first task with verification (`flutter --version` must succeed before proceeding). The `flutter analyze` success criterion (Success Criterion 4) cannot be verified without Flutter installed.

**Warning signs:** `flutter: command not found` when running Wave 0 scaffold task.

### Pitfall 6: QTcpServer Requires QCoreApplication to Be Running

**What goes wrong:** Unit tests that instantiate `AirShowHandler` and call `start()` without a `QCoreApplication` fail with `QSocketNotifier: Can only be used with threads started with QThread`.

**Why it happens:** `QTcpServer` uses `QSocketNotifier` internally, which requires the Qt event loop infrastructure.

**How to avoid:** `test_airshow.cpp` must use the `MiracastTestEnvironment` pattern — create a static `QCoreApplication` in a `testing::Environment::SetUp()`. This is already established in `test_miracast.cpp`.

---

## Code Examples

### AirShowHandler Header Skeleton

```cpp
// Source: modeled on src/protocol/MiracastHandler.h (established pattern)
#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QByteArray>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace airshow {

class ConnectionBridge;
class MediaPipeline;
class SecurityManager;

class AirShowHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    enum class State { Idle, Handshake, Streaming };

    static constexpr uint16_t kAirShowPort     = 7400;
    static constexpr int      kFrameHeaderSize = 16;

    explicit AirShowHandler(ConnectionBridge* connectionBridge, QObject* parent = nullptr);
    ~AirShowHandler() override;

    bool        start() override;
    void        stop() override;
    std::string name() const override { return "airshow"; }
    bool        isRunning() const override { return m_running; }
    void        setMediaPipeline(MediaPipeline* pipeline) override;
    void        setSecurityManager(SecurityManager* sm);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleHandshake();
    void handleStreamingData();
    void processFrame(const QByteArray& frameData);  // header + payload
    void disconnectClient();

    QTcpServer*       m_server     = nullptr;
    QTcpSocket*       m_client     = nullptr;
    ConnectionBridge* m_connectionBridge;
    MediaPipeline*    m_pipeline   = nullptr;
    SecurityManager*  m_security   = nullptr;
    State             m_state      = State::Idle;
    bool              m_running    = false;
    QByteArray        m_readBuffer;
};

} // namespace airshow
```

### DiscoveryManager Addition

```cpp
// Source: src/discovery/DiscoveryManager.cpp — add to start() after _display._tcp block
static constexpr uint16_t kAirShowPort = 7400;
std::vector<TxtRecord> airshowTxt = {
    {"ver", "1"},
    {"fn",  name},
};
m_advertiser->advertise("_airshow._tcp", name, kAirShowPort, airshowTxt);
```

### Test Scaffold Pattern

```cpp
// Source: modeled on tests/test_miracast.cpp (established test pattern)
#include <gtest/gtest.h>
#include <QCoreApplication>
#include "protocol/AirShowHandler.h"

static int s_argc = 1;
static const char* s_argv[] = {"test_airshow", nullptr};

class AirShowTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QCoreApplication::instance())
            new QCoreApplication(s_argc, const_cast<char**>(s_argv));
    }
};
testing::Environment* const airshowEnv =
    testing::AddGlobalTestEnvironment(new AirShowTestEnvironment);

TEST(AirShowHandlerTest, ConformsToInterface) {
    airshow::AirShowHandler handler(nullptr);
    EXPECT_EQ(handler.name(), "airshow");
    EXPECT_FALSE(handler.isRunning());
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

TEST(AirShowHandlerTest, ParseFrameHeader) {
    // Build a 16-byte header: type=0x01, flags=0x01, length=128, pts=33333333
    QByteArray header(16, 0);
    header[0] = 0x01;  // VIDEO_NAL
    header[1] = 0x01;  // keyframe
    qToBigEndian<quint32>(128, reinterpret_cast<uchar*>(header.data()) + 2);
    qToBigEndian<qint64>(33333333LL, reinterpret_cast<uchar*>(header.data()) + 6);
    // Verify parser reads these correctly (white-box test via parseFrameHeader static method)
    // ... see PLAN for full test body
    GTEST_SKIP() << "Stub — implemented in Plan 01";
}

TEST(AirShowHandlerTest, HandshakeJsonRoundTrip) {
    GTEST_SKIP() << "Stub — requires loopback TCP — implemented in Plan 02";
}

TEST(AirShowHandlerTest, AirShowMdnsAdvertisement) {
    GTEST_SKIP() << "Integration test — requires avahi-daemon — verified manually";
}
```

---

## Runtime State Inventory

> This is a greenfield protocol handler addition — not a rename/refactor/migration phase.

**Stored data:** None — no new persistent data keys. `kAirShowPort` may be added to `AppSettings` as a configurable value; if so, it uses `QSettings` (same as existing keys). No migration needed.

**Live service config:** None.

**OS-registered state:** None — AirShow protocol port is not firewall-registered. `WindowsFirewall::registerRules()` in `platform/WindowsFirewall.cpp` must be updated to include port 7400 TCP. This is a code edit (no runtime migration needed).

**Secrets/env vars:** None.

**Build artifacts:** None — adding a new `.cpp`/`.h` pair does not stale any existing build artifacts.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt6::Network (`QTcpServer`) | AirShowHandler TCP listener | Yes | 6.9.2 | — |
| Qt6::Core (`QJsonDocument`) | JSON handshake | Yes | 6.9.2 | — |
| Avahi (`avahi-client`) | `_airshow._tcp` mDNS | Yes | 0.8 | — |
| GStreamer appsrc | NAL unit injection | Yes | 1.26.x (confirmed ≥1.20) | — |
| GTest | test_airshow.cpp | Yes | 1.17.0 | — |
| Flutter SDK | `sender/` scaffold + `flutter analyze` | **No** | — | Wave 0 must install: `snap install flutter --classic` |

**Missing dependencies with no fallback:**
- Flutter SDK: required for Success Criterion 4 (`flutter analyze` on `sender/`). No fallback — must install before Wave 0 scaffold task executes.

**Missing dependencies with fallback:**
- None.

**Port 7400 conflict note:** Port 7400 is the RTPS/DDS default discovery port (IANA: `rtps-disc`). On this machine it is not in use. On ROS 2 developer machines it will conflict. Plan should make port configurable (see Pitfall 1).

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | GTest 1.17.0 + GMock |
| Config file | `tests/CMakeLists.txt` (each test target manually declared) |
| Quick run command | `cd /home/sanya/Desktop/MyAirShow/build && ctest -R test_airshow -V` |
| Full suite command | `cd /home/sanya/Desktop/MyAirShow/build && ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RECV-01 | Handler listens on port 7400; `start()`/`stop()` lifecycle | unit | `ctest -R AirShowHandlerTest.ConformsToInterface` | Wave 0 |
| RECV-01 | 16-byte binary frame header parses correctly | unit | `ctest -R AirShowHandlerTest.ParseFrameHeader` | Wave 0 |
| RECV-03 | Handshake JSON round-trip: HELLO in → HELLO_ACK out with correct fields | integration (loopback) | `ctest -R AirShowHandlerTest.HandshakeJsonRoundTrip` | Wave 0 |
| RECV-02 | `_airshow._tcp` appears in `avahi-browse` | integration (manual) | `avahi-browse -t _airshow._tcp` (manual after run) | — |
| RECV-01 + RECV-03 | NAL units from TCP connection appear on display (appsrc) | smoke (manual) | Run binary + raw TCP client sending frames | — |

### Sampling Rate

- **Per task commit:** `ctest -R test_airshow -V` (unit tests only, < 5 seconds)
- **Per wave merge:** `ctest --output-on-failure` (full suite)
- **Phase gate:** Full suite green + manual `avahi-browse` + NAL unit display test before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/test_airshow.cpp` — covers RECV-01, RECV-03 (unit stubs)
- [ ] `tests/CMakeLists.txt` addition — `add_executable(test_airshow ...)` block matching existing handler test pattern
- [ ] Flutter SDK install: `snap install flutter --classic` — required for Success Criterion 4

*(All other test infrastructure — GTest, CMake scaffold, `testing::Environment` pattern — already exists.)*

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hand-rolled TCP framing with `std::string` | `QByteArray` accumulator with `qFromBigEndian<T>` | Established by MiracastHandler (Phase 8) | Correct endian handling on all platforms; no buffer overruns |
| Separate JSON library (RapidJSON, nlohmann) | `QJsonDocument` (Qt6::Core) | Established by CastSession (Phase 6) | Zero additional dependency; already present in binary |
| Separate mDNS advertiser per handler | All services in `DiscoveryManager::start()` | Established by Phase 2 | Collision handling and rename propagate automatically |

**Deprecated/outdated (not applicable to this phase):**
- None specific to this phase.

---

## Open Questions

1. **Port 7400 configurability scope**
   - What we know: 7400 conflicts with RTPS/DDS on ROS 2 machines. The current codebase hardcodes protocol ports as `static constexpr` in each handler and as `static constexpr` in `DiscoveryManager.cpp`.
   - What's unclear: Whether to add `kAirShowPort` to `AppSettings` now (user-configurable) or keep it `static constexpr` (simpler). Other ports (AirPlay: 7000, Cast: 8009, MS-MICE: 7250) are not user-configurable.
   - Recommendation: Keep `static constexpr` in `AirShowHandler` for Phase 9 (consistent with all existing handlers). Add a `TODO` comment noting the ROS 2 conflict. User-configurable ports can be deferred to a future settings phase.

2. **Flutter SDK installation method**
   - What we know: Flutter is not installed on this machine. Snap is available (`snap find flutter` found the package). `snap install flutter --classic` should work on Ubuntu.
   - What's unclear: Whether the project CI/CD (GitHub Actions) already has Flutter in the workflow matrix for future phases.
   - Recommendation: Wave 0 plan should include Flutter installation as the first step with a `flutter --version` verification. Do not block C++ tasks on this — Flutter scaffold can be a parallel Wave 0 task.

3. **Handshake delimiter: newline vs. length-prefix**
   - What we know: The handshake JSON is small (< 256 bytes typical). The binary framing starts after handshake completes.
   - What's unclear: Whether to use a 4-byte length prefix for the handshake JSON (more robust) or a simple `\n` delimiter (simpler to implement in Flutter).
   - Recommendation: Use `\n`-terminated JSON for the handshake only (simpler for both sides, no size mismatch edge case). Switch to 16-byte binary framing for all streaming data after ACK is sent. The Flutter sender should append `\n` to its HELLO JSON.

---

## Sources

### Primary (HIGH confidence)

- `src/protocol/MiracastHandler.h/.cpp` — Direct template for `AirShowHandler` structure; QTcpServer + Qt event loop pattern; frame accumulation with `QByteArray`
- `src/protocol/CastSession.cpp` — `QJsonDocument` usage pattern for JSON handshake (11 confirmed usages)
- `src/discovery/DiscoveryManager.cpp` — `m_advertiser->advertise()` pattern for adding `_airshow._tcp`; 4 existing service type registrations
- `src/pipeline/MediaPipeline.h` — `videoAppsrc()` / `audioAppsrc()` accessors confirmed; `initAppsrcPipeline()` confirmed present since Phase 4
- `.planning/STATE.md ## Decisions` — Locked: port 7400, 16-byte framing layout, Flutter 3.41.5
- `pkg-config --modversion Qt6Network` → `6.9.2` (confirmed 2026-03-31)
- `pkg-config --modversion avahi-client` → `0.8` (confirmed 2026-03-31)
- `ss -tlnp | grep 7400` → port clear (confirmed 2026-03-31)

### Secondary (MEDIUM confidence)

- [whatportis.com port 7400](https://whatportis.com/ports/7400_rtps-real-time-publish-subscribe-dds-discovery) — Port 7400 is RTPS/DDS discovery (`rtps-disc`); confirmed by SpeedGuide cross-reference
- [Qt Network Programming docs](https://doc.qt.io/qt-6/qtnetwork-programming.html) — QTcpServer/QTcpSocket patterns

### Tertiary (LOW confidence)

- WebSearch results on Flutter monorepo structure — Melos/pub-workspace patterns documented; not needed for Phase 9's simple `flutter create` scaffold

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all C++ dependencies confirmed on machine; Flutter SDK absence confirmed
- Architecture: HIGH — all patterns copied from existing codebase (`MiracastHandler`, `CastSession`, `DiscoveryManager`)
- Pitfalls: HIGH — port 7400 conflict verified via IANA sources; partial-read and multi-pipeline pitfalls confirmed from codebase inspection
- Flutter scaffold: MEDIUM — `flutter create` behavior is standard; exact `flutter analyze` output on fresh scaffold verified via Flutter docs but not run on this machine

**Research date:** 2026-03-31
**Valid until:** 2026-06-01 (stable stack; GStreamer/Qt LTS; only Flutter version could change)
