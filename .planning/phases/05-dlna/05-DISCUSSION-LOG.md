# Phase 5: DLNA - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 05-dlna
**Areas discussed:** Media playback approach, Supported media formats, Transport control depth, Architecture pattern
**Mode:** --auto (all selections made by recommended defaults)

---

## Media Playback Approach

| Option | Description | Selected |
|--------|-------------|----------|
| uridecodebin | GStreamer handles HTTP fetch, format detection, codec selection, buffering automatically | ✓ |
| appsrc + manual HTTP | Manually fetch media via HTTP, decode, push frames into appsrc | |

**User's choice:** [auto] uridecodebin (recommended default)
**Notes:** DLNA is a pull model — controller sends URL, renderer fetches. uridecodebin is the standard GStreamer pattern for URI-based playback and avoids reimplementing HTTP client + format detection.

---

## Supported Media Formats

| Option | Description | Selected |
|--------|-------------|----------|
| Broad — all common formats | H.264/MP4, MPEG-TS, MKV, AVI video + MP3, AAC, FLAC, WAV audio | ✓ |
| Minimal — core formats only | H.264/MP4 + MP3/AAC only | |

**User's choice:** [auto] Broad (recommended default)
**Notes:** uridecodebin handles format detection via GStreamer plugin registry — no extra code per format. Broad support improves compatibility with different DLNA controllers and media libraries.

---

## Transport Control Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Full set | Play, Stop, Pause, Seek, SetAVTransportURI, GetTransportInfo, GetPositionInfo, GetMediaInfo + RenderingControl volume/mute | ✓ |
| Basic | Play, Stop, SetAVTransportURI only | |

**User's choice:** [auto] Full set (recommended default)
**Notes:** DLNA controller apps (BubbleUPnP, foobar2000) expect the full action set. Incomplete implementations cause UI errors and broken playback controls on the controller side.

---

## Architecture Pattern

| Option | Description | Selected |
|--------|-------------|----------|
| New DlnaHandler : ProtocolHandler | Follows AirPlayHandler pattern; UpnpAdvertiser routes SOAP events to DlnaHandler | ✓ |
| Extend UpnpAdvertiser directly | Add SOAP logic directly to UpnpAdvertiser class | |

**User's choice:** [auto] DlnaHandler : ProtocolHandler (recommended default)
**Notes:** Follows established pattern from Phase 4. Keeps UpnpAdvertiser focused on discovery (single responsibility). DlnaHandler owns SOAP dispatch, pipeline control, and session lifecycle.

---

## Claude's Discretion

- Internal threading model for SOAP action handling
- Exact GStreamer pipeline element chain for uridecodebin
- SCPD XML content details
- UPnP eventing (GENA) implementation
- Error handling for unsupported codecs / unreachable URLs
- playbin vs custom uridecodebin pipeline choice
