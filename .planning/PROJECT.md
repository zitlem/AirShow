# AirShow

## What This Is

A free, open-source, cross-platform screen mirroring receiver — an AirServer alternative that costs nothing. It turns any computer (Linux, macOS, Windows) into a wireless display that accepts screen mirrors from phones, tablets, and other computers via AirPlay, Google Cast, Miracast, and DLNA.

## Core Value

Any device can mirror its screen to any computer, for free — no licenses, no subscriptions, no paywalls.

## Requirements

Full list and per-requirement status in `REQUIREMENTS.md`; plan and current status in `ROADMAP.md`.

Nothing has been verified end-to-end on a real sender device yet. Summary:

- Implemented, needs device testing: cross-platform build, receiver window + audio, mDNS/SSDP discovery, AirPlay, DLNA, Miracast (MS-MICE), AirShow protocol receiver, Android sender
- Known broken: Google Cast media receive (uses WebRTC; Chrome speaks Cast Streaming)
- Not started: iOS / macOS / Windows senders, local web interface

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
| C++17 + Qt 6.8 + GStreamer receiver | Protocol libraries (UxPlay, pupnp, openscreen) are C/C++; GStreamer gives cross-platform HW decode | Adopted |
| Flutter for the companion sender | One codebase for Android, iOS, macOS, Windows | Adopted |
| Cast auth via shanocast/AirReceiver precomputed signatures | Only known way to pass Chrome device auth without a Google-issued cert | Adopted; expires ~2027-12-21 |
| Cast media via openscreen `cast/streaming` | Chrome mirroring uses Cast Streaming (RTP + AES-CTR + RTCP ACKs), not WebRTC | Planned (roadmap step 4) |

---
*Last updated: 2026-10-16 — status reset to device-verified reality*
