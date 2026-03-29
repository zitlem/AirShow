# Phase 6: Google Cast - Research

**Researched:** 2026-03-28
**Domain:** CASTV2 protocol, WebRTC receiver (VP8/Opus/SRTP/DTLS), Cast authentication bypass, GStreamer webrtcbin, Qt6 TLS server
**Confidence:** MEDIUM — CASTV2 wire protocol is HIGH confidence; Cast authentication bypass is MEDIUM (legally grey, depends on precomputed signatures from third-party APK); webrtcbin DTLS receiver integration is MEDIUM (plugin available but VP8 decode requires extra package)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Cast Authentication**
- **D-01:** Use a self-signed TLS certificate for the Cast receiver's TLS socket on port 8009.
- **D-02:** Accept that some Android devices may refuse to connect to uncertified receivers. Chrome browser tab casting and older Android versions are more permissive. Document this limitation clearly.
- **D-03:** Structure the auth layer so a real Google certificate can be dropped in later, without changing the protocol implementation.

**CASTV2 Protocol**
- **D-04:** Implement the CASTV2 protocol directly using protobuf + OpenSSL TLS, not via openscreen/libcast (GN+Ninja build system incompatibility with CMake is the blocker).
- **D-05:** Use `protobuf` (libprotobuf) for CASTV2 message serialization/deserialization.
- **D-06:** Implement namespace handlers: `urn:x-cast:com.google.cast.tp.connection`, `urn:x-cast:com.google.cast.tp.heartbeat`, `urn:x-cast:com.google.cast.receiver`, `urn:x-cast:com.google.cast.media`.
- **D-07:** For Cast mirroring implement `urn:x-cast:com.google.cast.webrtc` to handle WebRTC SDP offer/answer and ICE candidates.

**Media Pipeline**
- **D-08:** Cast screen mirroring uses WebRTC (VP8/VP9 video + Opus audio over SRTP/DTLS). Use GStreamer RTP depayload elements (`rtpvp8depay`, `rtpopusdepay`).
- **D-09:** Cast tab casting (Chrome) and Android screen mirror both use the same WebRTC path.
- **D-10:** For Cast media app content (YouTube URL via media namespace), use the `uridecodebin` approach from Phase 5.
- **D-11:** Use the existing `autoaudiosink` and `qml6glsink` video sink chain.

**Architecture**
- **D-12:** Create `CastHandler : ProtocolHandler` in `src/protocol/CastHandler.h`.
- **D-13:** CastHandler owns TLS server socket on port 8009, manages CASTV2 session state, dispatches namespace messages.
- **D-14:** Single-session model; new connection replaces active session.
- **D-15:** Session events routed to ConnectionBridge (shows "Cast" as protocol and sender device name).

### Claude's Discretion
- TLS certificate generation approach (runtime self-signed vs bundled)
- Internal threading model for the TLS server (Qt's QSslServer/QSslSocket is the natural choice given existing Qt dependency)
- Exact protobuf message definitions (copy from public CASTV2 documentation)
- WebRTC SDP negotiation details (ICE-lite vs full ICE)
- GStreamer element chain for VP8/Opus decoding
- Error handling for auth failures and unsupported Cast app types
- Whether to use `webrtcbin` GStreamer element or manual DTLS/SRTP handling

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope.
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CAST-01 | User can cast their Android device screen to MyAirShow via Google Cast | CASTV2 TLS server + auth bypass pattern documented; WebRTC VP8/Opus pipeline via GStreamer webrtcbin confirmed available; Android connectivity depends on auth approach |
| CAST-02 | User can cast a Chrome browser tab to MyAirShow via Google Cast | Chrome uses `enforce_nonce_checking=false` making auth bypass viable; Chrome Mirroring app uses `urn:x-cast:com.google.cast.webrtc` namespace with VP8/Opus |
| CAST-03 | Google Cast mirroring includes synchronized audio and video | GStreamer webrtcbin handles DTLS/SRTP/RTP transport with ICE; shared pipeline clock ensures A/V sync |
</phase_requirements>

---

## Summary

Google Cast uses the CASTV2 protocol: length-prefixed protobuf messages over TLS on port 8009. The wire protocol is well-documented through multiple open-source implementations (node-castv2, shanocast). The fundamental obstacle for open-source receivers is device authentication — Google requires a certificate chain signed by their "Eureka Gen1" CA that is not available to third parties.

The authentication bypass strategy, first documented in the shanocast blog and implemented in AirReceiver, exploits a flag in Google's own openscreen library (`enforce_nonce_checking=false`) that prevents Chrome from checking the nonce in challenge responses. This makes replay attacks viable: a fixed RSA key pair generates a self-signed peer certificate, and precomputed signatures (harvested from AirReceiver APK) matching that certificate are returned verbatim to Chrome. The signatures rotate every 48 hours and require approximately 45KB/year of storage. This approach works reliably with Chrome browser tab casting. Android device casting is less predictable — newer Android versions perform stricter certificate validation.

For media delivery, Cast screen mirroring from both Chrome and Android uses a non-standard WebRTC variant: the SDP offer/answer exchange happens inside CASTV2 JSON messages on the `urn:x-cast:com.google.cast.webrtc` namespace rather than via a WebSocket signaling server. The sender (Chrome/Android) sends an OFFER JSON containing VP8 video and Opus audio stream descriptors, SSRC assignments, and DTLS parameters. The receiver responds with an ANSWER. GStreamer's `webrtcbin` element handles the DTLS/SRTP/ICE transport layer and is available on this machine (gstreamer1.0-plugins-bad 1.26.5). A VP8 decode plugin (`gstreamer1.0-plugins-bad` for `vp8dec`) is required but `libvpx9` is already installed; the gst-libav fallback (`avdec_vp8`) is available via `libgstlibav.so`.

**Primary recommendation:** Implement `CastHandler` using Qt6's `QSslServer`/`QSslSocket` for TLS, libprotobuf for CASTV2 framing, the shanocast precomputed-signature pattern for auth, and GStreamer `webrtcbin` for WebRTC media reception. The Chrome browser Cast path (CAST-02) is achievable with medium-high confidence. The Android screen cast path (CAST-01) is achievable for older/less strict Android versions; document the limitation per D-02.

---

## Standard Stack

### Core (Phase 6 additions)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| **Qt6::Network** (QSslServer/QSslSocket) | 6.9.2 (system) | TLS server on port 8009, CASTV2 framing | Already a project dependency; QSslServer added in Qt 6.4, available on all platforms; avoids raw socket threading complexity |
| **libprotobuf** (system apt) | 3.21.12 (system) | CASTV2 CastMessage serialization | System package `libprotobuf-dev` 3.21.12 is available; CASTV2 uses a single tiny `.proto` file (`cast_channel.proto`) — no need for vcpkg |
| **protoc** | 3.21.12 (system) | Compile `cast_channel.proto` to C++ | Used at build time to generate `cast_channel.pb.h` / `.cc` from the proto definition |
| **GStreamer webrtcbin** | 1.26.5 (system) | WebRTC DTLS/SRTP/ICE transport + RTP reception | Included in `gstreamer1.0-plugins-bad` 1.26.5 already installed; handles DTLS handshake, SRTP decrypt, and ICE candidate negotiation |
| **libnice** (GStreamer ICE) | 0.1.22 (system) | ICE candidate gathering/connectivity for webrtcbin | `libnice10` 0.1.22 installed; webrtcbin depends on it internally |
| **libsrtp2** | 2.7.0 (system) | SRTP decrypt for webrtcbin | `libsrtp2-1` 2.7.0 already installed |
| **OpenSSL 3.x** | 3.x (existing) | Self-signed cert generation for TLS + DTLS | Already linked in Phase 1; used to generate peer cert at runtime |

### Supporting (already present)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `rtpvp8depay` (gst-plugins-good) | 1.26.5 | VP8 RTP depayloading | Always — webrtcbin delivers RTP packets on pad-added |
| `rtpopusdepay` (gst-plugins-good) | 1.26.5 | Opus RTP depayloading | Always — same webrtcbin pad-added path |
| `vp8dec` (gst-plugins-good) | via libvpx9 1.15.0 | VP8 software decode | Primary video decoder (hardware VP8 decode rare on desktop) |
| `avdec_vp8` (gst-libav) | via libgstlibav | VP8 FFmpeg fallback | Fallback if vp8dec unavailable |
| `opusdec` (gst-plugins-base) | 1.26.5 | Opus audio decode | Feeds into existing audioconvert ! audioresample ! autoaudiosink chain |
| `gstreamer1.0-nice` | 0.1.22 | GStreamer ICE plugin (nicesrc/nicesink) | **Not installed** — webrtcbin uses libnice directly, plugin not required for webrtcbin itself |

### Missing packages (require install)

| Package | Needed For | Install Command |
|---------|-----------|----------------|
| `libprotobuf-dev` | CASTV2 protobuf compile | `sudo apt install libprotobuf-dev protobuf-compiler` |
| `protobuf-compiler` | protoc code generation | included above |

Note: `gstreamer1.0-plugins-good` (which contains `vp8dec`, `rtpvp8depay`, `rtpopusdepay`) should be verified at runtime; `libvpx9` is installed but the GStreamer wrapper plugin needs the -good package.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| QSslServer | Raw OpenSSL BIO + std::thread | QSslServer integrates with Qt event loop, no manual threading; OpenSSL raw is more portable but more code |
| QSslServer | boost::asio + SSL | No boost dependency in the project; adds ~15MB; not warranted |
| libprotobuf (system) | vcpkg protobuf 6.33.4 | System 3.21.12 is sufficient for the tiny CastMessage proto; vcpkg protobuf drags in abseil/utf8-range; overkill |
| webrtcbin | manual DTLS/SRTP (libsrtp2 + OpenSSL DTLS) | webrtcbin encapsulates the DTLS handshake, ICE, and SRTP in ~20 lines; manual implementation is 500+ lines and error-prone |

**Installation (Phase 6 additions only):**
```bash
# Linux
sudo apt install libprotobuf-dev protobuf-compiler
# Verify webrtcbin is present (should already be from gst-plugins-bad)
gst-inspect-1.0 webrtcbin
# Verify VP8 decode
gst-inspect-1.0 vp8dec
```

---

## Architecture Patterns

### Recommended Project Structure (additions)

```
src/
├── protocol/
│   ├── CastHandler.h          # new: QObject + ProtocolHandler
│   ├── CastHandler.cpp        # new: TLS server, CASTV2 framing, session dispatch
│   ├── CastSession.h          # new: per-connection session state (namespaces, webrtcbin)
│   └── CastSession.cpp        # new: namespace dispatch, SDP exchange, auth
├── cast/
│   └── cast_channel.proto     # CASTV2 protobuf definition (generate once)
build/                         # cast_channel.pb.h and .pb.cc land here via protoc
```

### Pattern 1: CASTV2 Wire Format

**What:** Length-prefixed protobuf messages over TLS. Every message is: 4-byte big-endian uint32 (payload length) followed by a serialized `CastMessage` protobuf.

**When to use:** All communication on the port 8009 TLS socket.

**The proto definition (`cast_channel.proto`):**
```protobuf
// Source: Chromium source extensions/common/api/cast_channel/cast_channel.proto
syntax = "proto2";
package extensions.api.cast_channel;

message CastMessage {
  enum ProtocolVersion { CASTV2_1_0 = 0; }
  required ProtocolVersion protocol_version = 1;
  required string source_id = 2;
  required string destination_id = 3;
  required string namespace = 4;
  enum PayloadType { STRING = 0; BINARY = 1; }
  required PayloadType payload_type = 5;
  optional string payload_utf8 = 6;
  optional bytes payload_binary = 7;
}

message DeviceAuthMessage {
  optional AuthChallenge challenge = 1;
  optional AuthResponse response = 2;
  optional AuthError error = 3;
}

message AuthChallenge {
  optional SignatureAlgorithm signature_algorithm = 1 [default=RSASSA_PKCS1v15];
  optional bytes sender_nonce = 2;
  optional HashAlgorithm hash_algorithm = 3 [default=SHA1];
}

message AuthResponse {
  required bytes signature = 1;
  required bytes client_auth_certificate = 2;
  repeated bytes intermediate_certificate = 3;
  optional SignatureAlgorithm signature_algorithm = 4 [default=RSASSA_PKCS1v15];
  optional bytes sender_nonce = 5;
  optional HashAlgorithm hash_algorithm = 6 [default=SHA1];
}
```

**C++ read loop:**
```cpp
// Source: node-castv2 protocol documentation / oakbits.com
uint32_t len_be;
socket->read(reinterpret_cast<char*>(&len_be), 4);
uint32_t len = qFromBigEndian(len_be);  // Qt big-endian conversion
QByteArray data = socket->read(len);
extensions::api::cast_channel::CastMessage msg;
msg.ParseFromArray(data.constData(), data.size());
// dispatch on msg.namespace_()
```

### Pattern 2: Namespace Dispatcher

**What:** A `std::map<std::string, std::function<void(const CastMessage&)>>` routes messages to handler lambdas by namespace string.

**Core namespaces to implement:**

| Namespace | Messages | Response |
|-----------|----------|----------|
| `urn:x-cast:com.google.cast.tp.deviceauth` | `AuthChallenge` (binary payload) | `AuthResponse` with precomputed signature |
| `urn:x-cast:com.google.cast.tp.connection` | `{"type":"CONNECT"}` / `{"type":"CLOSE"}` | None (CONNECT), close socket (CLOSE) |
| `urn:x-cast:com.google.cast.tp.heartbeat` | `{"type":"PING"}` | `{"type":"PONG"}` |
| `urn:x-cast:com.google.cast.receiver` | `{"type":"GET_STATUS"}`, `LAUNCH`, `STOP` | `RECEIVER_STATUS` JSON with running apps |
| `urn:x-cast:com.google.cast.media` | `{"type":"GET_STATUS"}` | Empty media status |
| `urn:x-cast:com.google.cast.webrtc` | OFFER JSON | ANSWER JSON |

**Connection handshake sequence (Chrome tab cast):**
1. TLS handshake (receiver presents self-signed cert)
2. Sender: `deviceauth` namespace — AuthChallenge binary message
3. Receiver: `deviceauth` namespace — AuthResponse with precomputed signature
4. Sender: `connection` namespace — CONNECT (to `receiver-0`)
5. Sender: `heartbeat` namespace — PING
6. Receiver: `heartbeat` — PONG
7. Sender: `receiver` namespace — LAUNCH `0F5096E8` (Chrome Mirroring app ID)
8. Receiver: `receiver` — RECEIVER_STATUS (app launched, transportId assigned)
9. Sender: `connection` namespace — CONNECT (to virtual transportId)
10. Sender: `webrtc` namespace — OFFER JSON
11. Receiver: `webrtc` namespace — ANSWER JSON
12. SRTP/DTLS media flow begins

### Pattern 3: Cast Authentication Bypass

**What:** Return precomputed signatures from AirReceiver APK to satisfy Chrome's challenge without a Google-issued certificate.

**Why it works:** Chrome's openscreen has `enforce_nonce_checking = false`, meaning it verifies the signature against the peer certificate but does not check that the nonce in the signature matches the challenge nonce. This allows replay of any valid signature for a given peer certificate.

**Implementation approach (D-01, D-03):**
```cpp
// Source: shanocast blog (xakcop.com/post/shanocast/)
// 1. At startup, generate (or load cached) RSA-2048 key pair — fixed, not rotated
// 2. Generate a self-signed X.509 peer cert valid for 48h with that key
// 3. The TLS handshake uses a SEPARATE self-signed cert (different key) for TLS
// 4. On receiving AuthChallenge:
//    - Look up the precomputed signature matching today's peer cert validity window
//    - Build AuthResponse: signature = precomputed[date_index],
//                          client_auth_certificate = peer_cert_der
// 5. The peer cert must match what AirReceiver generates from the same RSA key

// Self-signed TLS cert (OpenSSL 3.x):
EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
EVP_PKEY_keygen_init(ctx);
EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
EVP_PKEY* tls_key = nullptr;
EVP_PKEY_generate(ctx, &tls_key);

X509* cert = X509_new();
X509_set_version(cert, 2);  // v3
ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
X509_gmtime_adj(X509_getm_notBefore(cert), 0);
X509_gmtime_adj(X509_getm_notAfter(cert), 60 * 60 * 48);  // 48h
X509_set_pubkey(cert, tls_key);
// ... set subject, sign with tls_key ...
X509_sign(cert, tls_key, EVP_sha256());
// Load into QSslConfiguration as localCertificate + privateKey
```

**Signature database:** Embed or load a binary blob of 795 precomputed 256-byte signatures. Index = `(now_unix / (48*3600))` modulo signature count. This covers years of operation with ~45KB of data.

### Pattern 4: QSslServer TLS Setup

**What:** Qt6's QSslServer (added Qt 6.4) provides TLS over QTcpServer. Requires `Qt6::Network`.

```cpp
// Source: Qt 6.9.2 docs (doc.qt.io/qt-6/qsslserver.html)
// Qt6::Network must be added to find_package and target_link_libraries

QSslConfiguration sslConfig;
sslConfig.setLocalCertificate(QSslCertificate(certPem));
sslConfig.setPrivateKey(QSslKey(keyPem, QSsl::Rsa));
sslConfig.setProtocol(QSsl::TlsV1_2OrLater);

QSslServer* server = new QSslServer(this);
server->setSslConfiguration(sslConfig);
server->listen(QHostAddress::Any, 8009);

connect(server, &QSslServer::pendingConnectionAvailable, this, [=]() {
    QSslSocket* socket = qobject_cast<QSslSocket*>(server->nextPendingConnection());
    // Read CASTV2 framing in readyRead slot
});
```

### Pattern 5: WebRTC via GStreamer webrtcbin (Receiver Mode)

**What:** GStreamer `webrtcbin` handles DTLS handshake, ICE connectivity checks, SRTP decrypt, and RTP demux. For Cast, ICE-lite is the recommended receiver mode (the receiver does not gather candidates; it accepts incoming DTLS-SRTP directly on the negotiated port).

**The Cast OFFER/ANSWER is NOT standard WebRTC SDP.** Chrome sends a proprietary JSON OFFER on the `webrtc` namespace (not a SDP blob). The receiver must translate this to a GStreamer-compatible SDP or use `webrtcbin` in a constrained mode.

**Options (for Claude's Discretion):**

**Option A — webrtcbin with translated SDP (RECOMMENDED):**
Translate the Cast OFFER JSON into a standard SDP string and feed it to `webrtcbin.set-remote-description`. Then call `create-answer`, get the GStreamer SDP answer, and translate back to Cast ANSWER JSON format.

```cpp
// Source: github.com/hissinger/gstreamer-webrtcbin-demo + webrtcHacks
GstElement* webrtcbin = gst_element_factory_make("webrtcbin", "castwebrtc");
g_object_set(webrtcbin, "bundle-policy", 3 /* GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE */, nullptr);

// After building SDP offer string from Cast JSON:
GstSDPMessage* sdp;
gst_sdp_message_new_from_text(sdp_offer_string, &sdp);
GstWebRTCSessionDescription* offer =
    gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);

GstPromise* promise = gst_promise_new_with_change_func(
    on_offer_set, this, nullptr);
g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, promise);

// In on_offer_set callback:
GstPromise* answer_promise = gst_promise_new_with_change_func(
    on_answer_created, this, nullptr);
g_signal_emit_by_name(webrtcbin, "create-answer", nullptr, answer_promise);

// Pad-added callback for incoming VP8/Opus:
g_signal_connect(webrtcbin, "pad-added", G_CALLBACK(on_incoming_pad), this);

// on_incoming_pad — creates decodebin that handles VP8/Opus dynamically:
static void on_incoming_pad(GstElement* /*webrtcbin*/, GstPad* pad, CastSession* self) {
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
    GstElement* decodebin = gst_element_factory_make("decodebin", nullptr);
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_decoded_pad), self);
    gst_bin_add(GST_BIN(self->pipeline()), decodebin);
    gst_element_sync_state_with_parent(decodebin);
    GstPad* sink = gst_element_get_static_pad(decodebin, "sink");
    gst_pad_link(pad, sink);
    gst_object_unref(sink);
}
```

**Option B — Manual DTLS/SRTP (NOT recommended for v1):**
Implement DTLS using OpenSSL's DTLS1_2_method, SRTP decrypt using libsrtp2, ICE using libnice directly. Viable but ~500 lines of C++ for correct ICE-lite + DTLS-SRTP interop. Defer to v2 if webrtcbin proves insufficient.

### Pattern 6: Cast OFFER JSON Translation to SDP

**What:** The Cast `urn:x-cast:com.google.cast.webrtc` OFFER JSON has this structure (from reverse engineering / node-castv2-client issue #14):

```json
{
  "type": "OFFER",
  "seqNum": 1,
  "offer": {
    "castMode": "mirroring",
    "receiverGetStatus": true,
    "supportedStreams": [
      {
        "index": 0,
        "type": "video_source",
        "codecName": "vp8",
        "rtpProfile": "cast",
        "rtpPayloadType": 96,
        "ssrc": 12345678,
        "aesKey": "<32 hex chars>",
        "aesIvMask": "<32 hex chars>",
        "maxFrameRate": "30000/1000",
        "maxBitRate": 4000000,
        "minBitRate": 300000,
        "maxWidth": 1280, "maxHeight": 720
      },
      {
        "index": 1,
        "type": "audio_source",
        "codecName": "opus",
        "rtpProfile": "cast",
        "rtpPayloadType": 97,
        "ssrc": 87654321,
        "aesKey": "<32 hex chars>",
        "aesIvMask": "<32 hex chars>",
        "sampleRate": 48000,
        "channels": 2
      }
    ]
  }
}
```

**Note:** The `aesKey`/`aesIvMask` fields indicate Cast uses its own AES-CTR encryption layer on top of SRTP. This is the Cast Streaming encryption, distinct from DTLS-SRTP. `webrtcbin` handles standard DTLS-SRTP but NOT Cast's additional AES-CTR layer. This is a known complexity gap — see Pitfall 4 below.

### Anti-Patterns to Avoid

- **Do not use openscreen/libcast:** GN+Ninja build system cannot be integrated into the CMake project without a separate build step. Decision D-04 locks this out.
- **Do not skip the heartbeat namespace:** Chrome disconnects within 30 seconds if PING goes unanswered. The heartbeat handler must reply with PONG immediately.
- **Do not use QSslSocket in blocking mode:** The CASTV2 read loop must be async (readyRead signal) — blocking reads on the Qt event thread stall all other Qt operations including the QML render.
- **Do not generate a new TLS cert per connection:** The TLS cert must be stable within a 48h window because it is what auth signatures are precomputed against. Regenerating on each connection breaks auth.
- **Do not mix protobuf 3.x and 4.x headers:** The system has protobuf 3.21.12. Do not mix with vcpkg protobuf 6.x (formerly 5.x). Use the system package exclusively.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| TLS server with certificate configuration | Custom SSL_CTX boilerplate | `QSslServer` + `QSslConfiguration` | Qt wraps OpenSSL cleanly; integrates with event loop |
| DTLS handshake + SRTP keying | OpenSSL DTLS + libsrtp2 manually | `webrtcbin` | webrtcbin handles DTLS-SRTP negotiation, SRTP decrypt, ICE; ~500 lines replaced by element creation |
| ICE connectivity (NAT traversal) | libnice directly | `webrtcbin` (uses libnice internally) | webrtcbin wraps libnice; ICE-lite is controlled via property |
| Protobuf message framing | Custom length-prefix codec | `libprotobuf` ParseFromArray + SerializeToString | Protobuf handles field encoding; custom parser will miss edge cases |
| VP8 RTP depayloading | Parse RTP headers manually | `rtpvp8depay` GStreamer element | RFC 6386 VP8 RTP payload has fragmentation/assembly complexity |
| Opus RTP depayloading | Parse RTP headers manually | `rtpopusdepay` GStreamer element | Same rationale |

**Key insight:** The Cast receiver is 80% protocol state machine and 20% media plumbing. Both parts have good library support. The only genuinely custom work is the CASTV2 namespace dispatcher and the OFFER/ANSWER JSON translation layer.

---

## Common Pitfalls

### Pitfall 1: Chrome vs Android Authentication Strictness

**What goes wrong:** Chrome browser tab casting works with the precomputed-signature bypass. Some Android versions (Android 12+) validate the certificate chain strictly and refuse to cast to uncertified receivers.

**Why it happens:** Chrome uses `enforce_nonce_checking=false` in openscreen, making the bypass viable. Android Cast sender may use stricter validation that checks the certificate chain up to Google's root CA.

**How to avoid:** Build the auth layer per D-03 so the certificate slot is swappable. Test Chrome first (CAST-02 before CAST-01). Document in the UI that Android screen cast may not work on all devices — this is expected per D-02.

**Warning signs:** Chrome tab cast works but Android shows "Can't connect to [device name]" or receiver never appears in Android's Cast menu despite mDNS advertisement working.

### Pitfall 2: Cast OFFER Uses Non-Standard WebRTC SDP — Not a Standard PeerConnection OFFER

**What goes wrong:** The `urn:x-cast:com.google.cast.webrtc` OFFER is a proprietary JSON format, not an SDP blob. Feeding it directly to `webrtcbin` will fail. The developer assumes the WebRTC namespace is standard WebRTC signaling.

**Why it happens:** Google Cast mirroring predates standardized WebRTC and uses its own JSON schema for stream negotiation.

**How to avoid:** Write an OFFER-to-SDP translator function that builds a valid SDP offer string from the Cast JSON fields (codec, SSRC, payload type, ICE credentials). Then feed the constructed SDP to `webrtcbin.set-remote-description`. The ANSWER JSON must be similarly constructed from the webrtcbin SDP answer.

**Warning signs:** `gst_sdp_message_new_from_text` fails because the OFFER is JSON, not SDP text. Or: webrtcbin reports "unexpected payload type" because Cast rtpPayloadType 96 is not in the SDP m-line.

### Pitfall 3: Cast AES-CTR Encryption Layer on Top of SRTP

**What goes wrong:** After DTLS-SRTP is established, Cast applies an additional AES-128-CTR encryption layer to the RTP payload (using the `aesKey`/`aesIvMask` from the OFFER). The webrtcbin element decrypts SRTP but does NOT decrypt the inner Cast AES-CTR layer. The result is garbled video/audio.

**Why it happens:** Cast Streaming uses two encryption layers: DTLS-SRTP for transport security + Cast-specific AES-CTR for content encryption. The AES-CTR layer is Google's own addition not part of standard WebRTC.

**How to avoid:** After `webrtcbin` delivers decoded RTP payloads via pad-added, add an AES-CTR decryption step before feeding VP8/Opus data to their respective decoders. Use OpenSSL's `EVP_aes_128_ctr` cipher with the SSRC-derived IV. The `aesKey` and `aesIvMask` from the OFFER JSON are the inputs. This decryption must happen between `rtpvp8depay` output and `vp8dec` input (and similarly for Opus).

**Warning signs:** Decoder receives data and runs but outputs completely corrupted video; no GStreamer errors (the data is valid RTP packaging but encrypted payload).

### Pitfall 4: Missing gstreamer1.0-nice Plugin

**What goes wrong:** `webrtcbin` requires both `libnice` (library, installed) and the GStreamer `nicesrc`/`nicesink` plugin (`gstreamer1.0-nice` package, NOT installed). Without the plugin, `webrtcbin` cannot gather ICE candidates and the element creation succeeds but connectivity fails silently.

**Why it happens:** `libnice10` is installed (version 0.1.22) but `gstreamer1.0-nice` (the GStreamer plugin wrapping libnice) is not. They are separate packages.

**How to avoid:** Add `gstreamer1.0-nice` to the installation instructions. Document in Wave 0 as a prerequisite. Add a runtime check in `CastHandler::start()` that calls `gst_registry_check_feature_version("nicesrc", 0, 1, 14)` and returns an error if missing.

**Install command:**
```bash
sudo apt install gstreamer1.0-nice
```

### Pitfall 5: Protobuf 3.21.12 System vs vcpkg Protobuf 6.x

**What goes wrong:** If vcpkg installs protobuf 6.x (the new 4.x/5.x/6.x series) and the system also has 3.21.12, linking both into the same binary will produce ODR violations (protobuf changed its ABI in 4.x with the protobuf-full vs protobuf-lite split restructuring). Linker may succeed but runtime will crash or corrupt messages.

**Why it happens:** protobuf 4.x (formerly called 3.22+) broke binary compatibility with 3.21.x. CMake picks up the first found version.

**How to avoid:** Use only the system `libprotobuf-dev` 3.21.12 and do not add protobuf to vcpkg.json. The CASTV2 proto definition is a single 30-field proto2 file — system protobuf is sufficient. In CMakeLists.txt, use `find_package(Protobuf REQUIRED)` (not CONFIG mode) to pick up the system installation.

### Pitfall 6: QSslSocket Blocking Read in readyRead Slot

**What goes wrong:** Reading all available bytes in a `readyRead` callback in a blocking loop causes partial reads to stall the Qt event loop. CASTV2 framing requires reading exactly `N` bytes for each message; TCP may deliver bytes in multiple chunks.

**Why it happens:** TCP is a stream protocol. A 4-byte length header and N-byte payload may arrive in separate `readyRead` signals.

**How to avoid:** Implement a state machine in the `readyRead` handler:
1. State READING_HEADER: accumulate until 4 bytes available, then parse length
2. State READING_PAYLOAD: accumulate until `length` bytes available, then dispatch message
Use `QByteArray m_buffer` as an accumulation buffer, not direct socket reads.

---

## Code Examples

### CASTV2 Length-Prefixed Send

```cpp
// Source: node-castv2 protocol documentation
void CastSession::sendMessage(const CastMessage& msg) {
    QByteArray payload(msg.ByteSizeLong(), 0);
    msg.SerializeToArray(payload.data(), payload.size());

    uint32_t len = qToBigEndian(static_cast<uint32_t>(payload.size()));
    m_socket->write(reinterpret_cast<const char*>(&len), 4);
    m_socket->write(payload);
}
```

### CASTV2 JSON Payload Convenience Builder

```cpp
// Build a STRING payload JSON message
CastMessage makeJsonMsg(const QString& src, const QString& dst,
                         const QString& ns, const QJsonObject& body) {
    CastMessage msg;
    msg.set_protocol_version(CastMessage::CASTV2_1_0);
    msg.set_source_id(src.toStdString());
    msg.set_destination_id(dst.toStdString());
    msg.set_namespace_(ns.toStdString());
    msg.set_payload_type(CastMessage::STRING);
    msg.set_payload_utf8(QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString());
    return msg;
}
```

### Heartbeat PONG Response

```cpp
// Source: CASTV2 namespace protocol documentation
void CastSession::onHeartbeat(const CastMessage& msg) {
    QJsonObject body = QJsonDocument::fromJson(
        QString::fromStdString(msg.payload_utf8()).toUtf8()).object();
    if (body["type"].toString() == "PING") {
        sendMessage(makeJsonMsg(
            "receiver-0", msg.source_id().c_str(),
            "urn:x-cast:com.google.cast.tp.heartbeat",
            {{"type", "PONG"}}));
    }
}
```

### RECEIVER_STATUS Response (for LAUNCH of Chrome Mirroring)

```cpp
// Source: CASTV2 receiver namespace documentation
QJsonObject CastSession::makeReceiverStatus(const QString& transportId) {
    return {
        {"type", "RECEIVER_STATUS"},
        {"status", QJsonObject{
            {"applications", QJsonArray{QJsonObject{
                {"appId", "0F5096E8"},
                {"displayName", "Chrome Mirroring"},
                {"namespaces", QJsonArray{
                    QJsonObject{{"name", "urn:x-cast:com.google.cast.webrtc"}},
                    QJsonObject{{"name", "urn:x-cast:com.google.cast.media"}}
                }},
                {"sessionId", "cast-session-1"},
                {"statusText", "Casting"},
                {"transportId", transportId}
            }}},
            {"volume", QJsonObject{{"level", 1.0}, {"muted", false}}}
        }}
    };
}
```

### Self-Signed TLS Certificate Generation (OpenSSL 3.x)

```cpp
// Source: OpenSSL 3.x EVP_PKEY documentation + OpenSSL gist examples
std::pair<QSslCertificate, QSslKey> CastHandler::generateSelfSignedCert() {
    // Generate RSA-2048 key
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_generate(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    // Generate X.509 v3 self-signed cert, valid 48h
    X509* x509 = X509_new();
    X509_set_version(x509, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_getm_notBefore(x509), 0);
    X509_gmtime_adj(X509_getm_notAfter(x509), 48 * 3600);
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (const unsigned char*)"MyAirShow", -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    // Convert to Qt types via PEM
    BIO* certBio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(certBio, x509);
    // ... read into QByteArray, construct QSslCertificate / QSslKey ...

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return {qtCert, qtKey};
}
```

### webrtcbin Receiver Setup Sketch

```cpp
// Source: gstreamer-webrtcbin-demo + GStreamer webrtcbin documentation
// (adapt for Cast's OFFER-to-SDP translation)
GstElement* webrtcbin = gst_element_factory_make("webrtcbin", "cast-webrtc");
g_object_set(webrtcbin, "stun-server", nullptr, nullptr);  // local-only, no STUN

g_signal_connect(webrtcbin, "pad-added",
    G_CALLBACK(on_webrtc_pad_added), this);
g_signal_connect(webrtcbin, "on-ice-candidate",
    G_CALLBACK(on_ice_candidate), this);

// After building sdpOfferStr from Cast JSON OFFER:
GstSDPMessage* sdp = nullptr;
gst_sdp_message_new_from_text(sdpOfferStr.c_str(), &sdp);
GstWebRTCSessionDescription* offer =
    gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
GstPromise* p = gst_promise_new_with_change_func(on_offer_set, this, nullptr);
g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, p);
gst_webrtc_session_description_free(offer);
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| openscreen as CMake sub-project | Direct CASTV2 protobuf implementation (D-04) | Locked by design decision | Removes 200K+ lines of Chromium build toolchain from dependencies |
| OpenSSL DTLS manual | webrtcbin (GStreamer 1.18+) | ~2018 (GStreamer WebRTC introduction) | DTLS-SRTP-ICE handled by element, not app code |
| QTcpServer + manual SSL | QSslServer (Qt 6.4+) | Qt 6.4 release 2022 | Cleaner TLS server API with Qt event loop integration |
| Protobuf 3.x (proto2) | Protobuf 4.x/5.x/6.x (proto3 default) | Protobuf 3.22 = 4.0 rebrand, 2023 | CastMessage uses proto2 syntax; stick with system 3.21.12 to avoid API churn |

**Deprecated/outdated:**
- `openscreen GN-only build`: Still GN-only in 2026; no CMake integration added. Decision D-04 correctly avoids it.
- `OpenSSL 1.1.1`: EOL September 2023. Project already uses 3.x.
- `QTcpServer + hand-rolled SSL`: Superseded by `QSslServer` (Qt 6.4+, available in project's Qt 6.9.2).

---

## Open Questions

1. **Cast AES-CTR Decryption Complexity**
   - What we know: Cast OFFER provides `aesKey`/`aesIvMask` per stream for a Cast-specific AES-128-CTR encryption layer on top of DTLS-SRTP. webrtcbin does not handle this layer.
   - What's unclear: Whether the AES-CTR layer is always present or only for encrypted Cast sessions. Some sources suggest it may be optional/skippable for open-source receivers that only handle DTLS-SRTP. If it IS required, the decrypt must happen between rtpvp8depay and vp8dec, requiring a custom GStreamer element or appsrc injection.
   - Recommendation: Implement without AES-CTR first and test with Chrome. If video is garbled, add the OpenSSL EVP_aes_128_ctr decrypt step. The AES-CTR key derivation (SSRC, seqnum, IV mask) is documented in openscreen's source and Cast protocol documentation.

2. **Android Sender Compatibility**
   - What we know: The precomputed-signature bypass works with Chrome (CAST-02). Android Cast sender behavior varies by OS version and device manufacturer.
   - What's unclear: Whether Android 12/13/14 senders perform strict chain verification or accept self-signed peer certs. No confirmed working open-source receiver implementation tested with recent Android.
   - Recommendation: Test Chrome first, declare CAST-02 as primary target. Document Android limitation per D-02. Flag CAST-01 as best-effort.

3. **VP8 GStreamer Plugin Package**
   - What we know: `libvpx9` 1.15.0 is installed. `libgstlibav.so` is present (contains `avdec_vp8` fallback). `gst-plugins-good` 1.26.5 is installed — but `libgstvpx.so` was not found at the expected path in initial check.
   - What's unclear: Whether `gstreamer1.0-plugins-good` includes the VP8 plugin in this distro's package or if `gstreamer1.0-plugins-bad` provides it. Runtime `gst-inspect-1.0 vp8dec` should confirm.
   - Recommendation: Wave 0 task must verify `vp8dec` availability and add install step if missing. Fallback: `avdec_vp8` via gst-libav.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt6::Network (QSslServer, QSslSocket) | TLS server on port 8009 | YES | 6.9.2 | — |
| libprotobuf-dev | CASTV2 CastMessage proto | NO | — | Install via apt |
| protobuf-compiler (protoc) | Generate pb.h from .proto | NO | — | Install via apt |
| GStreamer webrtcbin (.so) | WebRTC DTLS/SRTP | YES | 1.26.5 (plugin) | — |
| GStreamer webrtcbin dev headers | Compile webrtcbin calls | YES | 1.26.5 | — |
| gstreamer1.0-nice (GStreamer ICE plugin) | webrtcbin ICE candidate gathering | NO | — | Must install |
| libnice10 | ICE library (webrtcbin dependency) | YES | 0.1.22 | — |
| libsrtp2-1 | SRTP decrypt (webrtcbin dependency) | YES | 2.7.0 | — |
| libvpx9 | VP8 codec library | YES | 1.15.0 | — |
| vp8dec GStreamer plugin | VP8 video decode from webrtcbin | UNVERIFIED | ? | `avdec_vp8` via gst-libav |
| rtpvp8depay GStreamer plugin | VP8 RTP depayloading | EXPECTED (gst-plugins-good) | 1.26.5 | — |
| rtpopusdepay GStreamer plugin | Opus RTP depayloading | EXPECTED (gst-plugins-good) | 1.26.5 | — |
| OpenSSL 3.x | Self-signed cert generation | YES | linked Phase 1 | — |

**Missing dependencies with no fallback:**
- `libprotobuf-dev` + `protobuf-compiler` — required for CASTV2 framing. Install: `sudo apt install libprotobuf-dev protobuf-compiler`
- `gstreamer1.0-nice` — required for webrtcbin ICE. Install: `sudo apt install gstreamer1.0-nice`

**Missing dependencies with fallback:**
- `vp8dec` GStreamer plugin — if absent, use `avdec_vp8` (gst-libav, present).

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Google Test (GTest) — existing project standard |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `cd build && ctest -R test_cast -V` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CAST-01 | Android device connects and streams VP8/Opus to receiver | Integration / Manual | Manual: connect Android device to Cast menu | Manual only (requires device) |
| CAST-02 | Chrome tab cast connects, streams VP8/Opus, displays video | Integration / Manual | Manual: Chrome Cast tab to MyAirShow | Manual only (requires Chrome) |
| CAST-03 | A/V sync maintained during Cast session | Integration / Manual | Manual: cast video with known sync marker | Manual only |
| CAST-01/02 | CASTV2 framing (read/write length-prefixed protobuf) | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-01/02 | Namespace dispatcher routes messages correctly | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-01/02 | Heartbeat PING receives PONG response | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-01/02 | Auth bypass: AuthResponse has correct structure | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-01/02 | OFFER JSON parsed to SDP string correctly | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-01/02 | CastHandler::start() returns true when port 8009 opens | Unit | `ctest -R test_cast` | ❌ Wave 0 |
| CAST-03 | VP8/Opus pipeline decodes frames without errors | Smoke | `ctest -R test_cast` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && ctest -R test_cast -V`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/test_cast.cpp` — covers CASTV2 framing, namespace dispatch, auth structure, OFFER parsing, port binding, pipeline smoke
- [ ] `tests/CMakeLists.txt` — add `test_cast` target with CastHandler.cpp + CastSession.cpp + ConnectionBridge.cpp + MediaPipeline.cpp + protobuf link
- [ ] Package install: `sudo apt install libprotobuf-dev protobuf-compiler gstreamer1.0-nice`
- [ ] Proto codegen: `protoc --cpp_out=src/cast/ src/cast/cast_channel.proto` (run once, commit generated files)

---

## Project Constraints (from CLAUDE.md)

The following CLAUDE.md directives apply to this phase:

| Directive | Impact on Phase 6 |
|-----------|-------------------|
| Cost: completely free — no license keys | Do not use AirServer SDK or commercial Cast SDK. Use open-source shanocast auth bypass approach. |
| Local network only | No STUN/TURN servers; ICE should be local-network-only (disable STUN in webrtcbin config) |
| C++17 | CastHandler and CastSession use C++17 features; protobuf 3.21.12 is C++17 compatible |
| Qt 6.8 LTS | Project has Qt 6.9.2 installed; QSslServer available since 6.4 — fully compatible |
| GStreamer 1.26.x | webrtcbin in gst-plugins-bad 1.26.5 — already installed |
| OpenSSL 3.x | Runtime cert generation uses EVP_PKEY_CTX_new_from_name (OpenSSL 3.x API) |
| CMake >= 3.28 | protobuf_generate() CMake function used for proto codegen |
| Do NOT use Qt QMediaPlayer | Confirmed: Cast uses webrtcbin + custom pipeline, not QMediaPlayer |
| Do NOT use Platinum UPnP SDK | Not applicable to Cast phase |
| Do NOT embed openscreen | D-04 locks this out; confirmed correct decision |
| ProtocolHandler pure virtual interface | CastHandler must implement all 5 virtual methods: start(), stop(), name(), isRunning(), setMediaPipeline() |
| File-scope C trampolines for C callback APIs | webrtcbin C callbacks (pad-added, on-ice-candidate) follow the same trampoline pattern as AirPlayHandler |
| QMetaObject::invokeMethod for cross-thread GStreamer | webrtcbin callbacks fire from GStreamer threads; all Qt signal emissions and pipeline state changes must be marshalled via Qt::QueuedConnection |
| GSD workflow — no direct edits outside GSD | Use `/gsd:execute-phase` for implementation |

---

## Sources

### Primary (HIGH confidence)
- [node-castv2 GitHub (thibauts/node-castv2)](https://github.com/thibauts/node-castv2) — CASTV2 wire format: length-prefix, protobuf CastMessage schema, namespace list
- [oakbits.com — Google Cast Protocol Receiver Authentication](https://oakbits.com/google-cast-protocol-receiver-authentication.html) — Three-tier PKI, AuthChallenge/AuthResponse protobuf, 48h cert rotation
- [Qt 6 QSslServer documentation](https://doc.qt.io/qt-6/qsslserver.html) — QSslServer API, introduced Qt 6.4, Qt::Network module
- [GStreamer webrtcbin .so verification](https://gstreamer.freedesktop.org/documentation/webrtc/) — libgstwebrtc.so present at `/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstwebrtc.so`, gst-plugins-bad 1.26.5
- [gstreamer-webrtcbin-demo C source](https://github.com/hissinger/gstreamer-webrtcbin-demo/blob/main/webrtc-sendrecv.c) — on_incoming_stream pad-added pattern, set-remote-description / create-answer C API
- [OpenSSL 3.x EVP_PKEY-RSA docs](https://docs.openssl.org/3.0/man7/EVP_PKEY-RSA/) — EVP_PKEY_CTX_new_from_name keygen API for self-signed cert

### Secondary (MEDIUM confidence)
- [Shanocast blog (xakcop.com)](https://xakcop.com/post/shanocast/) — enforce_nonce_checking=false bypass; precomputed signature approach; AirReceiver signature harvest; MEDIUM because legally grey
- [Chromecast Device Authentication blog (tristanpenman.com, 2025-03-22)](https://tristanpenman.com/blog/posts/2025/03/22/chromecast-device-authentication/) — Certificate chain verified; practical bypass requires rooted Chromecast
- [Tab Mirroring over CASTV2 issue #14 (node-castv2-client)](https://github.com/thibauts/node-castv2-client/issues/14) — Chrome Mirroring app ID `0F5096E8`, OFFER JSON structure with VP8/Opus streams
- [RidgeRun GstWebRTC VP8-Opus Examples](https://developer.ridgerun.com/wiki/index.php?title=GstWebRTC_-_Vp8-Opus_Examples) — webrtcsrc/webrtcsink VP8+Opus pipeline structure
- [vcpkg protobuf package page](https://vcpkg.io/en/package/protobuf.html) — Latest vcpkg protobuf is 6.33.4; confirmed system 3.21.12 sufficient for CastMessage proto2

### Tertiary (LOW confidence — flag for validation)
- [Vjerci.com Chromecast protocol writeup](https://vjerci.com/writings/chromecast/protocol/) — Namespace list; Google docs "lacking"; based on reverse engineering
- AES-CTR layer on OFFER streams — documented in multiple sources but exact handling with webrtcbin unverified; may not be required for all Cast sessions

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries verified against system packages; Qt6::Network/QSslServer confirmed present; webrtcbin .so confirmed present
- Architecture patterns: MEDIUM — CASTV2 framing and namespace dispatch are HIGH; Auth bypass and OFFER/SDP translation are MEDIUM (tested in shanocast but implementation requires AES-CTR investigation)
- Pitfalls: MEDIUM — Cast AES-CTR and Android compatibility are the primary unknowns; all other pitfalls are HIGH confidence

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (30 days) — CASTV2 is stable; shanocast auth bypass could break if Google changes enforce_nonce_checking

---

*Phase: 06-google-cast*
*Researched: 2026-03-28*
