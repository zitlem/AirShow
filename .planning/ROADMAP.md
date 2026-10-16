# Roadmap: AirShow

**Status legend:** ✅ verified on a real device · 🟡 implemented, not device-tested · 🔴 known broken · ⬜ not started

## Where things stand (2026-10-16)

| Area | Status | Notes |
|------|--------|-------|
| Build (Linux / macOS / Windows) | 🟡 | macOS builds and launches (Qt 6.12, GStreamer 1.28, OpenSSL 4, protobuf 36); Linux/Windows not re-checked |
| Receiver window, HUD, idle screen | 🟡 | All pipelines render via `VideoFrameSink` (appsink → QImage); `qml6glsink` no longer required |
| Discovery (mDNS + SSDP) | 🟡 | All five mDNS services register on macOS; not yet seen in a sender's picker |
| AirPlay (iOS / macOS) | 🟡 | UxPlay embedded; recent crash fix for null callbacks on first `/info` probe |
| DLNA renderer | 🟡 | Registration fixed (was passing a file path as the description URL); description + SCPDs served over HTTP; video now reaches the screen |
| Google Cast | 🔴 | Auth signature verified against our TLS cert (automated test); media receive path is wrong (see step 4) |
| Miracast (MS-MICE, Windows) | 🟡 | Video now reaches the screen (was going to fakesink); Wi-Fi Direct deliberately unsupported |
| Security (approval, PIN, LAN-only) | 🟡 | Approval prompt on by default (SEC-01); Cast approval only prompts when a cast actually starts (step 3) |
| AirShow protocol receiver (port 7400) | 🟡 | JSON handshake + 16-byte framed H.264/audio |
| Android sender (Flutter) | 🟡 | Needs physical-device end-to-end test |
| iOS / macOS / Windows senders | ⬜ | |
| Web interface (port 7401) + distribution | ⬜ | |

## Plan

### 1. Working local build ✅ (macOS, 2026-10-16)
- Install deps on macOS: `brew install cmake ninja qt@6 gstreamer libplist libupnp`
- Build with `./build-and-run.sh` and run `ctest`
- **Done when:** the app launches and the test suite passes on this machine
- Result: launches; all 87 tests pass with `ctest -j1` (parallel runs collide on port 8009 / shared QSettings). Fixed along the way: UxPlay submodule not checked out, OpenSSL 4 const API, libupnp 22 callback type, protobuf ≥22 needing Abseil (now uses protobuf's CMake package), QML module dir colliding with the `airshow` binary on case-insensitive filesystems, `qml6glsink` hard requirement

### 2. Device testing baseline
Test every protocol on real hardware and record the result in the table above.
- AirPlay: iPhone and Mac
- DLNA: a controller app (e.g. BubbleUPnP) pushing a video and an audio file
- Miracast: Windows 10/11 "Connect to a wireless display"
- AirShow sender: Android app
- For each: connects, picture, sound, disconnect/reconnect, 30-minute session
- **Done when:** every 🟡 row is either ✅ or has a written bug

### 3. Cast quick fixes ✅ (2026-10-16)
- **TLS cert window rollover:** `CastHandler::generateSelfSignedCert()` runs once at startup, but the precomputed signature changes every 48 h. Regenerate the cert when the window changes so auth keeps working after the first two days.
- **Approval flow:** `CastSession::onConnection` drops the `CONNECT` that triggered the approval prompt (even if approved) and a second `CONNECT` during the prompt opens a duplicate dialog. Queue the pending message and replay it on approval; prompt once per session.
- **Signature table expiry:** `getSignatureForTime()` wraps modulo after ~2027-12-21, which silently fails auth. Return "no signature" past the end, stop advertising `_googlecast._tcp`, log why, and warn 90 days ahead. (Done; the warning is in the log only — a UI notice is still to do.)

### 4. Cast Streaming receive path
Chrome tab/desktop mirroring uses **Cast Streaming**, not WebRTC: plain RTP over UDP with a Cast-specific frame header, AES-128-CTR payload encryption (keys in the OFFER), and RTCP frame ACKs the sender requires to keep sending. The current `webrtcbin` + SDP translation cannot work, `udpPort` is sent as 0, and the stored AES keys are never used.
- Build openscreen `cast/streaming` (GN + Ninja) as a separate step and link its static libs
- Answer OFFER with a real UDP port, the chosen stream `index` values in `sendIndexes`, and receiver SSRCs
- Feed decoded-from-transport VP8/H.264 + Opus frames into the existing appsrc pipeline (same as AirPlay)
- Remove `buildSdpFromOffer`, `initWebrtcPipeline`, and related WebRTC code
- **Scope:** Chrome on desktop. Android "Cast screen" is not expected to accept the shanocast/AirReceiver credentials.
- **Done when:** a Chrome tab and full desktop mirror to AirShow with audio for 30 minutes

### 5. Companion senders and web interface
- Finish verifying the AirShow protocol receiver and Android sender (from step 2)
- iOS sender: Broadcast Upload Extension + VideoToolbox H.264, QR-code connect (needs research on extension → socket pattern first)
- macOS sender: ScreenCaptureKit + VideoToolbox, stable Developer ID signing so TCC permission persists
- Windows sender: Windows.Graphics.Capture with DXGI fallback, Media Foundation H.264
- Web interface on port 7401: receiver name, QR code, offline installer downloads

The AirShow sender is the long-term path for Chrome/Android users because Cast auth has a hard expiry and can be revoked by Google at any time.

### 6. Release hardening
- GitHub Actions matrix builds for Linux, macOS, Windows
- Packaging per platform (bundle Bonjour on Windows)
- Repeat step 2 on each platform

## Known external risks

- **Cast device auth** relies on a precomputed signature table extracted from AirReceiver (via shanocast). It covers 2023-08-15 → ~2027-12-21 and cannot be extended without a newer table. Google can also revoke the device cert or enforce nonce checking at any time.
- **Miracast over Wi-Fi Direct** remains out of scope; most Android devices only support Wi-Fi Direct.
