# AirShow

## What This Is

A free, open-source, cross-platform screen mirroring receiver — an AirServer alternative that costs nothing. It turns any computer (Linux, macOS, Windows) into a wireless display that accepts screen mirrors from phones, tablets, and other computers via AirPlay, Google Cast, Miracast, and DLNA.

## Core Value

Any device can mirror its screen to any computer, for free — no licenses, no subscriptions, no paywalls.

## Requirements

### Validated

- ✓ Run on Linux, macOS, and Windows — Phase 1 (build system + cross-platform presets)
- ✓ Display mirrored content in a fullscreen receiver window — Phase 1 (GStreamer + qml6glsink)
- ✓ Play audio from mirrored devices with mute toggle — Phase 1 (autoaudiosink + AudioBridge)
- ✓ Receive AirPlay screen mirroring from iOS/macOS devices — Phase 4 (UxPlay RAOP + appsrc injection, pending human device testing)
- ✓ Receive DLNA media streams — Phase 5 (DlnaHandler + uridecodebin + AVTransport SOAP, pending human device testing)

### Active

- ✓ Receive Google Cast screen mirroring from Android/Chrome devices — Phase 6 (CastHandler + CASTV2 + WebRTC, pending auth signature extraction + human testing)
- ✓ Receive Miracast screen mirroring from Windows/Android devices — Phase 8 (MS-MICE over Infrastructure, pending Windows device testing)
- ✓ Auto-discover and advertise as a receiver on the local network — Phase 2 (mDNS + SSDP)
- ✓ Android companion sender app — Phase 10 (Flutter BLoC + Kotlin MediaProjection/H264Encoder + AirShow protocol, pending physical device E2E testing)

### Out of Scope

- ~~Streaming FROM the computer to other devices~~ — now in scope for v2.0 (companion sender app)
- Cloud/remote mirroring over the internet — local network only
- Recording or capturing mirrored content — display only for v1
- Mobile app versions (iOS/Android receiver) — desktop only

## Current Milestone: v2.0 AirShow Companion Sender

**Goal:** Build a cross-platform companion app that discovers AirShow receivers on the network and mirrors the device screen to them.

**Target features:**
- Flutter-based sender app for Android, iOS, Windows, macOS, Linux
- mDNS discovery of AirShow receivers on local network
- Screen capture and H.264 encoding using platform-native APIs
- Custom AirShow protocol for sender-to-receiver streaming
- New AirShowHandler protocol handler on the receiver side

## Context

- AirServer is the dominant player but costs $20+ per license
- Open-source alternatives exist for individual protocols (e.g., uxplay for AirPlay) but nothing unifies all protocols into one receiver
- The opportunity is a single app that "just works" regardless of which device you're casting from
- DLNA was added as a protocol to cover smart TV-style media push use cases

## Constraints

- **Cost**: Must be completely free — no freemium, no ads, no license keys
- **Cross-platform**: Must work on Linux, macOS, and Windows from the same codebase
- **Network**: Local network only (mDNS/Bonjour discovery, no internet required)
- **License**: Open source (specific license TBD)

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Fullscreen receiver UI (minimal) | User wants a TV-like experience, not a complex settings app | — Pending |
| All major protocols (AirPlay, Cast, Miracast, DLNA) | Differentiation over single-protocol alternatives | — Pending |
| Tech stack TBD | Will be decided during research phase | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd:transition`):
1. Requirements invalidated? -> Move to Out of Scope with reason
2. Requirements validated? -> Move to Validated with phase reference
3. New requirements emerged? -> Add to Active
4. Decisions to log? -> Add to Key Decisions
5. "What This Is" still accurate? -> Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-04-01 — Phase 10 complete: Android sender MVP with BLoC discovery, Kotlin MediaProjection/H264 capture pipeline, receiver audio wiring*
