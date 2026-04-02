---
phase: 10-android-sender-mvp
plan: 03
subsystem: receiver-audio
tags: [gstreamer, appsrc, audio, pcm, s16le, airshow-protocol, cpp, unit-test]

requires:
  - phase: 10-android-sender-mvp
    plan: 02
    provides: Android sender sends type=0x02 audio frames (raw PCM S16LE 44100Hz stereo) over TCP AirShow framing

provides:
  - AirShowHandler.cpp audio frame injection: type=0x02 frames pushed into MediaPipeline::audioAppsrc() via gst_app_src_push_buffer
  - PCM audio caps (audio/x-raw,format=S16LE,rate=44100,channels=2,layout=interleaved) set on first audio frame
  - m_audioCapSet flag for one-time caps initialization, reset on disconnect for clean reconnect
  - AudioFrameHeaderParsed unit test verifying type=0x02 header parsing

affects: [11-ios-sender, receiver-pipeline]

tech-stack:
  added: []
  patterns:
    - One-time caps initialization via m_audioCapSet bool flag reset on disconnectClient()
    - gst_app_src_push_buffer pattern mirrored from video branch (kTypeVideoNal) for audio branch (kTypeAudio)

key-files:
  created: []
  modified:
    - src/protocol/AirShowHandler.cpp
    - src/protocol/AirShowHandler.h
    - tests/test_airshow.cpp

decisions:
  - PCM caps set lazily on first audio frame (not at pipeline init) — avoids caps negotiation failure when audio frames arrive before pipeline is ready

metrics:
  duration: ~6 min
  completed: 2026-04-02
  tasks_completed: 2
  files_modified: 3
---

# Phase 10 Plan 03: Audio Frame Injection Summary

**One-liner:** Receiver-side audio pipeline wired: type=0x02 PCM frames from Android sender pushed into GStreamer audioAppsrc with S16LE 44100Hz stereo caps.

## What Was Built

The AirShowHandler.cpp `processFrame()` method previously dropped all type=0x02 audio frames with a "not yet implemented" placeholder. This plan wired the audio path:

1. On first audio frame: `m_pipeline->setAudioCaps("audio/x-raw,format=S16LE,rate=44100,channels=2,layout=interleaved")` is called once via `m_audioCapSet` flag.
2. Each subsequent audio frame: a `GstBuffer` is allocated, filled with the PCM payload, stamped with the PTS from the frame header, and pushed via `gst_app_src_push_buffer(GST_APP_SRC(m_pipeline->audioAppsrc()), buf)`.
3. On disconnect: `m_audioCapSet = false` resets state so reconnecting senders correctly reinitialize caps.

The `AudioFrameHeaderParsed` unit test validates that a 16-byte header with type=0x02, flags=0x00, length=4096, pts=1000000 is correctly parsed by `AirShowHandler::parseFrameHeader()`.

## Tasks

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Wire audio frame injection in AirShowHandler.cpp processFrame() | 1f139aa | AirShowHandler.cpp, AirShowHandler.h, test_airshow.cpp |
| 2 | End-to-end verification on Android device | — (auto-approved, checkpoint) | — |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None. The audio injection is fully wired. End-to-end verification (Task 2) requires a physical Android device and is a human-verify checkpoint that was auto-approved per `auto_advance: true`.

## Self-Check: PASSED

- src/protocol/AirShowHandler.cpp: modified (audio injection present)
- src/protocol/AirShowHandler.h: modified (m_audioCapSet member present)
- tests/test_airshow.cpp: modified (AudioFrameHeaderParsed test present)
- Commit 1f139aa: verified present
- All 4 test_airshow tests pass (exit 0)
