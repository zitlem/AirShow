# Phase 6: Google Cast - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 06-google-cast
**Areas discussed:** Cast authentication approach, Cast protocol implementation, Media streaming pipeline, Session management
**Mode:** --auto (all selections made by recommended defaults)

---

## Cast Authentication Approach

| Option | Description | Selected |
|--------|-------------|----------|
| Certificate-free with fallback | Self-signed TLS, accept limited compatibility, structure for future cert | ✓ |
| Google Cast SDK developer program | Apply for certified device status — blocks on Google approval | |
| Skip Cast entirely | Too risky legally/technically — defer to v2 | |

**User's choice:** [auto] Certificate-free with fallback (recommended default)
**Notes:** Open-source project can't get Google device certificates. Chrome and older Android are more permissive. Structure auth layer for future cert drop-in.

---

## Cast Protocol Implementation

| Option | Description | Selected |
|--------|-------------|----------|
| Direct CASTV2 protobuf | Implement protocol directly with protobuf + OpenSSL TLS | ✓ |
| openscreen/libcast | Google's Cast library — GN build system, hard CMake integration | |

**User's choice:** [auto] Direct CASTV2 protobuf (recommended default)
**Notes:** openscreen uses GN+Ninja which doesn't integrate with CMake project. CASTV2 is simple: length-prefixed protobuf over TLS.

---

## Media Streaming Pipeline

| Option | Description | Selected |
|--------|-------------|----------|
| WebRTC with GStreamer RTP | Use rtpvp8depay/rtpopusdepay for screen mirror; uridecodebin for media URLs | ✓ |
| webrtcbin element | GStreamer's webrtcbin handles full WebRTC stack | |

**User's choice:** [auto] WebRTC with GStreamer RTP (recommended default)
**Notes:** Cast mirroring sends VP8/VP9+Opus over WebRTC/SRTP. For media URLs, reuse DLNA's uridecodebin pattern.

---

## Session Management

| Option | Description | Selected |
|--------|-------------|----------|
| CastHandler : ProtocolHandler | Follow established pattern from AirPlay/DLNA | ✓ |

**User's choice:** [auto] CastHandler : ProtocolHandler (recommended default)
**Notes:** Consistent with all other protocol handlers. Single-session, ConnectionBridge for HUD.

---

## Claude's Discretion

- TLS certificate generation approach
- Internal threading model for TLS server
- Protobuf message definitions
- WebRTC SDP negotiation details
- GStreamer element chain for VP8/Opus
- Error handling for auth failures
- webrtcbin vs manual DTLS/SRTP
