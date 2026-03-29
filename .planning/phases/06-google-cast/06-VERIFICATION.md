---
phase: 06-google-cast
verified: 2026-03-28T00:55:00Z
status: gaps_found
score: 4/6 must-haves verified
re_verification: false
gaps:
  - truth: "Chrome browser can initiate a Cast tab session to MyAirShow"
    status: failed
    reason: "Auth bypass uses placeholder RSA-2048 signatures that Chrome will reject. The code structure and wiring is complete but kCastAuthSignatures and kCastAuthPeerCert in cast_auth_sigs.h are deterministic placeholder bytes, not signatures extracted from AirReceiver APK. Chrome validates the AuthResponse signature as RSA-2048 PKCS#1v1.5 and will deny the connection."
    artifacts:
      - path: "src/cast/cast_auth_sigs.h"
        issue: "kCastAuthSignatures contains placeholder data (CAST_SIG_ROW macro pattern). kCastAuthPeerCert is a 2-byte DER prefix only. Chrome rejects Cast auth with these values."
    missing:
      - "Real 795x256-byte RSA-2048 signature table extracted from AirReceiver APK"
      - "Real DER-encoded peer certificate (~800-1200 bytes) that the signatures were computed against"
  - truth: "AES-CTR decryption is applied to Cast-encrypted RTP payloads when aesKey is present"
    status: failed
    reason: "setCastDecryptionKeys() stores keys in m_castCryptoKeys map but no decrypt element is ever inserted in the GStreamer pad-added chain. The code path from key storage to actual decryption is missing. If a Cast sender sends encrypted RTP, video will be garbled."
    artifacts:
      - path: "src/pipeline/MediaPipeline.cpp"
        issue: "onWebrtcPadAdded creates video and audio chains (rtpvp8depay -> vp8dec -> ... and rtpopusdepay -> opusdec -> ...) without checking or inserting any decrypt step between depayloader and decoder, even when m_castCryptoKeys contains keys for the stream's SSRC."
    missing:
      - "Conditional AES-CTR decrypt element (identity with handoff, or appsink+appsrc pair) inserted between depayloader and decoder in onWebrtcPadAdded when keys are present for the stream SSRC"
human_verification:
  - test: "Chrome Cast tab -- end-to-end with real signatures"
    expected: "Chrome tab content appears in receiver window, audio plays through speakers, HUD shows Cast + device name, disconnect returns to idle"
    why_human: "Requires replacing placeholder signatures with real AirReceiver APK data and testing with a live Chrome browser"
  - test: "Android device Cast screen mirror"
    expected: "Android screen appears in receiver window with audio"
    why_human: "Requires an Android device and real Cast auth credentials; certificate validation differs from Chrome tab cast"
  - test: "A/V sync over extended Cast session"
    expected: "No observable audio/video drift after 5+ minutes of Cast mirroring"
    why_human: "Runtime behavior cannot be verified statically; requires real media session"
---

# Phase 6: Google Cast Verification Report

**Phase Goal:** Android devices and Chrome browser tabs can cast their screen to MyAirShow with synchronized audio and video
**Verified:** 2026-03-28T00:55:00Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | An Android device can select MyAirShow from the Cast menu and mirror its screen to the receiver | ? UNCERTAIN | mDNS advertisement of `_googlecast._tcp` on port 8009 is wired (DiscoveryManager.cpp:95). CastHandler binds port 8009 (CastHandler.cpp:69). CASTV2 handshake implemented. Auth will fail with placeholder signatures — connection cannot complete. |
| 2 | Chrome browser "Cast tab" sends a browser tab to MyAirShow for display | FAILED | Full CASTV2 control plane is implemented (TLS server, framing, namespace dispatch, OFFER/ANSWER SDP translation, webrtcbin pipeline). Auth bypass uses placeholder RSA signatures — Chrome will reject the DeviceAuthMessage response. Tab cast cannot complete until real signatures replace placeholders in cast_auth_sigs.h. |
| 3 | Audio from the casting device plays through the receiver's speakers in sync with the video | ? UNCERTAIN | GStreamer webrtcbin pipeline with VP8+Opus decode chains is implemented. Audio chain: `rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink`. Play() transitions both pipelines to PLAYING simultaneously. Cannot verify A/V sync without a live session (blocked by auth). Additionally: AES-CTR decrypt is not inserted even when keys are present — encrypted sessions would produce garbled output. |
| 4 | CastHandler listens on TLS port 8009 with a self-signed certificate | VERIFIED | CastHandler.cpp:69 `m_server->listen(QHostAddress::Any, 8009)`. Self-signed RSA-2048 cert generated via OpenSSL EVP API (CastHandler.cpp:43). Integration test CastHandler_IntegrationStartStop confirms port 8009 binding (16/16 tests pass). |
| 5 | CastHandler is registered in ProtocolManager and starts on application launch | VERIFIED | main.cpp:134-140 — scoped block creates CastHandler with `window.connectionBridge()` and calls `protocolManager.addHandler(std::move(castHandler))`. Pattern matches AirPlay/DLNA registration. Plugin checks for webrtcbin, rtpvp8depay, rtpopusdepay, opusdec added (fatal) and vp8dec/nicesrc (non-fatal warnings). |
| 6 | HUD shows Cast protocol and device name during active session; disconnect returns to idle screen | VERIFIED | CastSession.cpp:225-226 calls `m_connectionBridge->setConnected(true, m_senderName, QStringLiteral("Cast"))` via QMetaObject::invokeMethod(QueuedConnection) on transportId CONNECT. Disconnect: setConnected(false) called at lines 237 and 691. |

**Score:** 4/6 truths verified (2 failed/uncertain due to placeholder auth and missing AES-CTR decrypt chain)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/cast/cast_channel.proto` | CastMessage + DeviceAuthMessage protobuf definitions | VERIFIED | 70 lines. Contains CastMessage, AuthChallenge, AuthResponse, AuthError, DeviceAuthMessage. Correct proto2 syntax. |
| `src/cast/cast_auth_sigs.h` | Precomputed 256-byte RSA-2048 signatures | STUB (by design) | Exists. kCastAuthSignatures table is 795x256 bytes of placeholder data (CAST_SIG_ROW macro). kCastAuthPeerCert is 2 bytes. Code structure correct; binary data must be replaced with real APK-extracted signatures before Chrome auth works. |
| `src/protocol/CastHandler.h` | ProtocolHandler implementation for Google Cast | VERIFIED | QObject+ProtocolHandler, QSslServer*, generateSelfSignedCert() private method, all 5 ProtocolHandler virtuals declared. |
| `src/protocol/CastHandler.cpp` | TLS server, connection accept, CastSession lifecycle | VERIFIED | 243 lines (above 80 min). listen(port 8009), generateSelfSignedCert via EVP_PKEY, pendingConnectionAvailable signal. |
| `src/protocol/CastSession.h` | Per-connection CASTV2 session state machine | VERIFIED | 108 lines. ReadState enum, all 6 namespace handler declarations, sendMessage/makeJsonMsg/buildSdpFromOffer. |
| `src/protocol/CastSession.cpp` | CASTV2 framing, namespace dispatch, auth, heartbeat, receiver status, media LOAD, WebRTC OFFER/ANSWER | VERIFIED | 779 lines (above 200 min). All 6 namespace handlers implemented. buildSdpFromOffer public static. Full onWebrtc() OFFER handling. |
| `src/pipeline/MediaPipeline.h` | WebRTC pipeline mode with setQmlVideoItem | VERIFIED | setQmlVideoItem, initWebrtcPipeline, setRemoteOffer, getLocalAnswer, setCastDecryptionKeys, webrtcbin() all declared. CastCryptoKeys struct. |
| `src/pipeline/MediaPipeline.cpp` | webrtcbin pipeline creation, pad-added VP8/Opus decode chains | VERIFIED (partial) | 1102 lines. webrtcbin created, VP8 chain (rtpvp8depay/vp8dec/avdec_vp8 fallback), Opus chain (rtpopusdepay/opusdec). gst_sdp_message, set-remote-description, create-answer all present. AES-CTR keys stored but not applied. |
| `tests/test_cast.cpp` | Unit tests for CASTV2 framing, namespace dispatch, auth structure | VERIFIED | 512 lines (above 50 min). 16 tests total: framing round-trip, auth response structure, signature index rotation, handler lifecycle, SDP translation (3 tests), WebRTC pipeline init, AES key storage, integration tests. All 16 pass. |
| `src/main.cpp` | CastHandler registration and lifecycle wiring | VERIFIED | #include "protocol/CastHandler.h", CastHandler registered via addHandler pattern. Plugin checks for webrtcbin/rtpvp8depay/rtpopusdepay/opusdec (fatal) and vp8dec/nicesrc (non-fatal). |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/protocol/CastHandler.cpp` | QSslServer on port 8009 | QSslServer::listen + pendingConnectionAvailable signal | WIRED | CastHandler.cpp:69 `m_server->listen(QHostAddress::Any, 8009)` |
| `src/protocol/CastSession.cpp` | cast_channel.pb.h | CastMessage::ParseFromArray / SerializeToArray | WIRED | CastSession.cpp uses CastMessage throughout dispatch loop; ParseFromArray in dispatchMessage (line ~112); SerializeToArray in sendMessage |
| `src/protocol/CastSession.cpp` | cast_auth_sigs.h | kCastAuthSignatures[date_index] lookup | WIRED | CastSession.cpp:164-165 `size_t idx = (nowSecs / 172800) % kCastAuthSignatureCount; const uint8_t* sig = &cast::kCastAuthSignatures[idx][0];` |
| `src/protocol/CastSession.cpp` | ConnectionBridge | setConnected on session start/end | WIRED | CastSession.cpp:225 setConnected(true, m_senderName, "Cast"); 237,691 setConnected(false). All via QMetaObject::invokeMethod(QueuedConnection). |
| `src/protocol/CastSession.cpp` | MediaPipeline::initWebrtcPipeline | onWebrtc OFFER calls m_pipeline->initWebrtcPipeline() | WIRED | CastSession.cpp:604 `m_pipeline->initWebrtcPipeline()` (no argument — uses stored m_qmlVideoItem) |
| `src/main.cpp` | src/protocol/CastHandler.cpp | protocolManager.addHandler(std::move(castHandler)) | WIRED | main.cpp:134-138 scoped block |
| `src/main.cpp` | src/ui/ConnectionBridge.h | window.connectionBridge() passed to CastHandler constructor | WIRED | main.cpp:136-137 `CastHandler(window.connectionBridge())` |
| `src/pipeline/MediaPipeline.cpp` | webrtcbin element | gst_element_factory_make("webrtcbin") | WIRED | MediaPipeline.cpp:739 onWebrtcPadAdded callback; webrtcbin created in initWebrtcPipeline |
| `src/pipeline/MediaPipeline.cpp` | vp8dec / avdec_vp8 | pad-added -> VP8 decode chain | WIRED | MediaPipeline.cpp:782-784 vp8dec with avdec_vp8 fallback |
| `src/protocol/CastSession.cpp` | MediaPipeline AES-CTR keys | setCastDecryptionKeys on aesKey field | PARTIAL | Keys stored correctly (CastSession.cpp:584). Decrypt element never inserted in onWebrtcPadAdded. Data does not flow through decryption. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `src/protocol/CastSession.cpp` | CASTV2 messages | QSslSocket readyRead -> accumulation buffer -> ParseFromArray | Real (when auth succeeds) | BLOCKED — auth requires real signatures |
| `src/pipeline/MediaPipeline.cpp` | VP8 video frames | webrtcbin pad-added -> rtpvp8depay -> vp8dec -> qml6glsink | Real (when session established) | BLOCKED — no live Cast session possible with placeholder auth |
| `src/pipeline/MediaPipeline.cpp` | Opus audio frames | webrtcbin pad-added -> rtpopusdepay -> opusdec -> autoaudiosink | Real (when session established) | BLOCKED — no live Cast session possible with placeholder auth |
| `src/cast/cast_auth_sigs.h` | kCastAuthSignatures | Placeholder CAST_SIG_ROW macro data | No — placeholder bytes only | STATIC — real RSA signatures not embedded |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 16 unit tests pass (framing, auth, SDP, pipeline, integration) | `/home/sanya/Desktop/MyAirShow/build/tests/test_cast` | 16/16 PASSED | PASS |
| Main binary builds without errors | `ninja myairshow` in build dir | Build succeeded (30/30 targets) | PASS |
| CastHandler binds port 8009 (integration test) | CastHandler_IntegrationStartStop in test_cast | port 8009 bound, TLS cert generated | PASS |
| App launches and logs Cast port | `./myairshow` | Would log "Cast handler started on port 8009" | SKIP — requires display server |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|---------|
| CAST-01 | 06-01-PLAN.md, 06-02-PLAN.md, 06-03-PLAN.md | User can cast their Android device screen to MyAirShow via Google Cast | BLOCKED | Full protocol stack implemented. Auth fails with placeholder signatures — Android device cannot complete Cast handshake. |
| CAST-02 | 06-01-PLAN.md, 06-02-PLAN.md, 06-03-PLAN.md | User can cast a Chrome browser tab to MyAirShow via Google Cast | BLOCKED | Full CASTV2 + WebRTC pipeline implemented. Chrome discovers MyAirShow (mDNS). Auth rejected by Chrome due to placeholder RSA signatures in cast_auth_sigs.h. |
| CAST-03 | 06-02-PLAN.md, 06-03-PLAN.md | Google Cast mirroring includes synchronized audio and video | NEEDS HUMAN | webrtcbin handles DTLS-SRTP with single pipeline clock for A/V sync. Cannot verify without a live authenticated Cast session. AES-CTR decrypt chain missing for encrypted sessions. |

No orphaned requirements: all 3 Cast requirements (CAST-01, CAST-02, CAST-03) are claimed across plans and cross-referenced in REQUIREMENTS.md traceability table.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/cast/cast_auth_sigs.h` | 34-36 | TODO comment and placeholder data: "Replace with real signatures extracted from AirReceiver APK" | BLOCKER | Chrome validates RSA-2048 PKCS#1v1.5 signatures cryptographically. Placeholder bytes fail verification. CAST-01 and CAST-02 cannot succeed. |
| `src/cast/cast_auth_sigs.h` | ~100+ | kCastAuthPeerCert is 2-byte DER prefix only (placeholder) | BLOCKER | AuthResponse.client_auth_certificate will be rejected by Chrome as an invalid X.509 certificate. |
| `src/pipeline/MediaPipeline.cpp` | 739-870 | onWebrtcPadAdded inserts VP8/Opus chains without AES-CTR decrypt even when m_castCryptoKeys contains keys | WARNING | Encrypted Cast sessions (aesKey field present in OFFER) will produce garbled video/audio. Non-encrypted sessions (no aesKey) are unaffected. Real Chrome tab casts may or may not use AES-CTR; field testing required. |

### Human Verification Required

#### 1. Chrome Tab Cast with Real Signatures

**Test:** Extract RSA-2048 signatures and peer certificate from AirReceiver APK, replace cast_auth_sigs.h content, rebuild. Open Chrome, click Cast > Sources > Cast tab. Select MyAirShow from the Cast dialog.
**Expected:** Chrome tab content appears in receiver window. Audio plays through speakers. HUD overlay shows "Cast" and the Chrome device name. Clicking Stop in Chrome returns receiver to idle screen.
**Why human:** Requires real AirReceiver APK extraction (out-of-band tooling), a live Chrome browser, and visual/audio confirmation. Cannot be verified programmatically with placeholder data.

#### 2. Android Device Screen Cast

**Test:** On Android device on the same LAN, pull down notification shade and tap Cast / Screen Mirror. Select MyAirShow.
**Expected:** Android screen appears in receiver window with synchronized audio.
**Why human:** Requires physical Android device. Android Cast uses slightly different certificate validation than Chrome. Also blocked by placeholder signatures.

#### 3. A/V Sync Over Extended Cast Session

**Test:** Cast a video with clear audio/video sync markers (e.g., a metronome video or clapping sync test) from Chrome for 10+ minutes.
**Expected:** No observable audio/video drift throughout the session.
**Why human:** Runtime behavior; GStreamer pipeline clock synchronization cannot be statically verified. Requires live media.

#### 4. AES-CTR Encrypted Session Behavior

**Test:** Once real auth works, capture a Cast session and verify whether aesKey field is present in the OFFER JSON. If present, verify video is not garbled.
**Expected:** If encrypted session: video displays correctly (decrypt chain working). If unencrypted: video works as-is.
**Why human:** Requires live session to observe whether Chrome sends aesKey. If present, the missing decrypt chain (anti-pattern #3) becomes a BLOCKER.

### Gaps Summary

Two gaps block the phase goal:

**Gap 1 — Placeholder authentication signatures (BLOCKER for CAST-01 and CAST-02)**

The Cast device authentication bypass requires 795 precomputed RSA-2048 PKCS#1v1.5 signatures extracted from the AirReceiver APK. The code structure is fully implemented — `kCastAuthSignatures[idx]` lookup, `kCastAuthPeerCert` embedding, and `AuthResponse` construction are all wired correctly. Only the binary data is wrong. Chrome performs cryptographic validation of the AuthResponse and will reject placeholder bytes unconditionally. This is a known documented decision (Plans 01-03 all note it as a stub), but it means CAST-01 and CAST-02 cannot be demonstrated to work until real signatures are provided.

**Gap 2 — Missing AES-CTR decrypt chain in GStreamer pipeline (WARNING for CAST-03)**

`setCastDecryptionKeys()` correctly stores hex-decoded AES-128 keys per SSRC. However, `onWebrtcPadAdded` never reads `m_castCryptoKeys` and never inserts a decrypt step between the depayloader and decoder. CAST-03 ("synchronized audio and video") would fail for any Cast session that uses Cast streaming encryption. Whether Chrome tab casts actually send `aesKey` in their OFFER is unknown (per RESEARCH.md Open Question 1) — this requires field testing once auth is resolved. The infrastructure (key storage map, hex parser) is in place; only the pipeline insertion is missing.

The entire CASTV2 control plane, WebRTC media pipeline, SDP translation, HUD wiring, plugin checks, and test coverage are well-implemented. The gaps are concentrated in one known stub (auth signatures) and one deferred implementation decision (AES-CTR decrypt application).

---

_Verified: 2026-03-28T00:55:00Z_
_Verifier: Claude (gsd-verifier)_
