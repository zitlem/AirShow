---
phase: 08-miracast
plan: 02
subsystem: protocol/miracast
tags: [miracast, ms-mice, wfd, rtsp, state-machine, security, pipeline]
dependency_graph:
  requires: [08-01]
  provides: [full-ms-mice-wfd-rtsp-flow, security-integration, pipeline-start-stop]
  affects: [src/protocol/MiracastHandler.h, src/protocol/MiracastHandler.cpp, tests/test_miracast.cpp]
tech_stack:
  added: []
  patterns:
    - WFD M1-M7 RTSP state machine as QTcpSocket async state machine (non-blocking accumulation buffer)
    - checkConnectionAsync() for Qt-thread protocol handlers (same as CastHandler)
    - parseNextRtspMessage() accumulation buffer parser (same pattern as CastSession ReadState)
    - kWfdCapabilityResponse as static constexpr const char* for test access
    - setQmlVideoItem() for deferred QML item injection from main.cpp
key_files:
  created: []
  modified:
    - src/protocol/MiracastHandler.cpp
    - src/protocol/MiracastHandler.h
    - tests/test_miracast.cpp
decisions:
  - sendRtspRequest() takes extraHeaders param to pass Transport/Require headers without polluting body
  - parseNextRtspMessage() replaces simple delimiter scan — handles both requests and responses uniformly
  - buildRtspResponse() now includes "Server: AirShow/1.0" header for spec compliance
  - teardown() resets to WaitingSourceReady (not Idle) when m_running=true — server stays listening
  - connectToSource() resets m_cseq=1 and clears m_rtspBuffer on each new session
metrics:
  duration: 1m
  completed_date: "2026-03-30"
  tasks_completed: 2
  files_modified: 3
---

# Phase 8 Plan 02: MiracastHandler Full MS-MICE + WFD-RTSP Implementation Summary

**One-liner:** Full MS-MICE two-phase connection flow with WFD M1-M7 RTSP state machine, SecurityManager async approval, MPEG-TS pipeline start/stop, and ConnectionBridge HUD integration.

## What Was Built

### Task 1: MS-MICE SOURCE_READY flow + SecurityManager + RTSP client connection

Implemented the complete MiracastHandler protocol state machine, transforming the Plan 01 skeleton into a fully functional receiver. Key changes:

**MiracastHandler.h additions:**
- `setQmlVideoItem(void* item)` — deferred QML item injection for `initMiracastPipeline()` (same pattern as `MediaPipeline::setQmlVideoItem()` used by Cast in Plan 03)
- `kWfdCapabilityResponse` — static constexpr M3 capability response body with all WFD fields; `wfd_content_protection: none` per RESEARCH.md Pitfall 6
- `connectToSource()`, `teardown()`, `parseNextRtspMessage()` — private methods
- `RtspMessage` struct — holds parsed request/response fields for state machine dispatch
- `m_qmlVideoItem` member

**MiracastHandler.cpp — new implementations:**

- `parseMiceMessage()` — after parsing SOURCE_READY, checks `isLocalNetwork()` (SEC-03) then calls `checkConnectionAsync()` on Qt thread (same async pattern as CastHandler to avoid event loop deadlock)
- `connectToSource()` — creates `QTcpSocket`, connects signals, calls `connectToHost(m_sourceAddr, m_rtspPort)`, resets CSeq and buffer
- `onRtspConnected()` — transitions to `NegotiatingM1`, waits for source M1 OPTIONS
- `onRtspData()` — accumulation buffer loop; calls `parseNextRtspMessage()` repeatedly; dispatches to state-specific handling
- WFD M1-M7 state machine:
  - M1 (NegotiatingM1): responds 200 OK with Public header, sends M2 OPTIONS (Pitfall 3 — M2 is NOT optional), transitions to NegotiatingM2
  - M2 (NegotiatingM2): receives 200 OK, transitions to NegotiatingM3
  - M3 (NegotiatingM3): receives GET_PARAMETER, responds with `kWfdCapabilityResponse`, transitions to NegotiatingM4
  - M4 (NegotiatingM4): receives SET_PARAMETER (selected codec), responds 200 OK, transitions to NegotiatingM5
  - M5 (NegotiatingM5): receives SET_PARAMETER trigger, responds 200 OK, sends M6 SETUP with `Transport: RTP/AVP/UDP;unicast;client_port=1028`, transitions to SendingSetup
  - M6 (SendingSetup): receives 200 OK, calls `initMiracastPipeline(m_qmlVideoItem, m_udpPort)`, sends M7 PLAY, transitions to SendingPlay
  - M7 (SendingPlay): receives 200 OK, calls `m_pipeline->play()`, calls `m_connectionBridge->setConnected(true, m_sourceName, "miracast")`, transitions to Streaming
  - Streaming: handles GET_PARAMETER keepalives (200 OK) and TEARDOWN (200 OK then teardown())
- `teardown()` — stops pipeline, clears HUD, disconnects sockets, resets to WaitingSourceReady
- `sendRtspRequest()` — formats `METHOD uri RTSP/1.0\r\nCSeq: N\r\n{extraHeaders}\r\n{body}`
- `parseNextRtspMessage()` — parses one complete RTSP message from accumulation buffer, extracts method/status/CSeq/contentLength/body, removes consumed bytes

### Task 2: WFD RTSP state machine unit tests

Added 5 tests to `tests/test_miracast.cpp` (Tests 5-9):

- **Test 5: WfdCapabilityResponseFormat** — verifies all 6 WFD fields present, `wfd_content_protection: none`, no HDCP string
- **Test 6: RtspRequestFormat** — verifies M2 OPTIONS format (method/CSeq/Require header) and M6 SETUP Transport header format
- **Test 7: StateTransitionsOnSourceReady** — verifies SOURCE_READY extraction of name/port/sourceId, rejects STOP_PROJECTION command (0x02), rejects truncated messages
- **Test 8: M3ResponseIncludesAllFields** — verifies each WFD field line ends with `\r\n`, H.264 CBP+CHP markers, LPCM+AAC audio, port 1028
- **Test 9: TeardownResetsState** — verifies `stop()` sets `isRunning()` false, idempotent second stop

**All 9 tests pass** (4 from Plan 01 + 5 new).

## Commits

| Hash | Message |
|------|---------|
| `407266c` | feat(08-02): implement full MS-MICE + WFD-RTSP state machine in MiracastHandler |
| `4bed77c` | test(08-02): add WFD RTSP state machine unit tests (Tests 5-9) |

## Deviations from Plan

### Auto-fixed Issues

None.

### Implementation Notes

**1. sendRtspRequest() signature extended**

The plan specified `sendRtspRequest(method, uri, body)` but the M1 response uses a `Public:` header (not a body), and M2 OPTIONS needs `Require:` header, M6 SETUP needs `Transport:` header. Added `extraHeaders` parameter to avoid a second helper function. Consistent with the 3-argument signature documented but extensible.

**2. M1 200 OK uses inline response (not buildRtspResponse)**

The M1 200 OK requires a `Public:` header which buildRtspResponse doesn't handle. Built the M1 response inline using QStringLiteral format. buildRtspResponse remains the standard helper for normal 200/400/454/500 responses without custom headers.

**3. parseNextRtspMessage() handles both requests and responses**

The plan described separate request/response parsing. Unified into one method with `isRequest` boolean in `RtspMessage` struct. Simpler and more consistent — the state machine checks `msg.isRequest` and `msg.method` / `msg.statusCode` as needed.

## Known Stubs

None — all state machine transitions are fully implemented. The `m_qmlVideoItem` will be null until main.cpp calls `setQmlVideoItem()` in Plan 03, but `initMiracastPipeline()` already handles `nullptr` with `fakesink` fallback (from Plan 01 MediaPipeline implementation).

## Self-Check: PASSED

- src/protocol/MiracastHandler.cpp — FOUND
- src/protocol/MiracastHandler.h — FOUND
- tests/test_miracast.cpp — FOUND
- commit 407266c (feat: MS-MICE + WFD-RTSP state machine) — FOUND
- commit 4bed77c (test: WFD RTSP unit tests) — FOUND
