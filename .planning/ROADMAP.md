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
| Google Cast | 🟡 | Own Cast Streaming receiver (step 4) passes loopback tests; needs a real Chrome test. Auth signature verified against our TLS cert |
| Miracast (MS-MICE, Windows) | 🟡 | Video reaches the screen; audio decodes AAC and LPCM. Open question: Windows may need Wi-Fi Direct for discovery (see risks) |
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

### 4. Cast Streaming receive path — implemented (2026-10-16), needs a Chrome test
Chrome tab/desktop mirroring uses **Cast Streaming**, not WebRTC: plain RTP over UDP with a Cast-specific frame header, AES-128-CTR payload encryption (keys in the OFFER), and RTCP frame ACKs the sender requires to keep sending (it stalls after ~66 ms of unacknowledged media).
- Linking openscreen was dropped: it bundles BoringSSL, whose symbols clash with our OpenSSL. Instead `src/cast/CastStreamingReceiver` implements the receiver directly, following openscreen's BSD-licensed `cast/streaming/impl` wire formats (RTP Cast header, frame ID expansion, IV derivation, RR/RRTR/PLI/Cast Feedback with NACKs and ACKs).
- `src/cast/CastStreamingOffer` parses the OFFER, picks VP8 (then VP9, H.264) + Opus, and builds the ANSWER (real UDP port, `sendIndexes`, receiver SSRC = sender SSRC + 1). `GET_CAPABILITIES` gets a "remoting not supported" reply.
- Frames play through `MediaPipeline::initCastPipeline` (appsrc → decodebin → VideoFrameSink). WebRTC code removed.
- Tests: OFFER/ANSWER, a loopback sender (encrypted multi-packet frames, packet loss → NACK → retransmit, checkpoint advance), 8-bit frame ID wrap, VP8/Opus decode.
- Not yet done: Receiver Report loss/jitter stats are zero; no lip-sync from Sender Reports (frames are timestamped on arrival); runs on the GUI thread.
- **Scope:** Chrome on desktop. Android "Cast screen" is not expected to accept the shanocast/AirReceiver credentials.
- **Done when:** a Chrome tab and full desktop mirror to AirShow with audio for 30 minutes

### 5. Companion senders and web interface
- Finish verifying the AirShow protocol receiver and Android sender (from step 2)
- iOS sender: Broadcast Upload Extension + VideoToolbox H.264, QR-code connect (needs research on extension → socket pattern first)
- macOS sender: ScreenCaptureKit + VideoToolbox, stable Developer ID signing so TCC permission persists
- Windows sender: Windows.Graphics.Capture with DXGI fallback, Media Foundation H.264
- Web interface on port 7401: receiver name, QR code, offline installer downloads

The AirShow sender is the long-term path for Chrome/Android users because Cast auth has a hard expiry and can be revoked by Google at any time.

### 6. Release hardening — in progress (issue #1: Windows build / binaries)
- GitHub Actions: Linux tests (Ubuntu 24.04), macOS build+tests, Windows (MSYS2 UCRT64) build+tests
- No-install downloads built by CI, attached to GitHub releases on `v*` tags:
  - Windows: portable zip (`packaging/windows/package.sh`: windeployqt + GStreamer plugins + DLLs)
  - macOS: `AirShow.app` in a .dmg (`packaging/macos/package.sh`: macdeployqt, GStreamer plugins, rpath rewrite, ad-hoc signed; Apple Silicon only; not notarized)
  - Linux: AppImage built on Ubuntu 22.04 (`packaging/linux/package.sh`: linuxdeploy + Qt/GStreamer plugins)
- Windows builds without the Bonjour SDK; pupnp is built from source where no libupnp package exists (MSYS2)
- No Bonjour needed at runtime either: `MdnsResponder` (src/discovery) is AirShow's own mDNS/DNS-SD responder (RFC 6762/6763: probing with conflict rename, announcements, goodbyes, known-answer suppression, legacy unicast, IPv4), used when the system has no mDNS service — Windows without Bonjour, Linux without avahi-daemon. `AIRSHOW_MDNS=builtin` forces it. UxPlay's Bonjour-based `dnssd.c` is replaced by `src/airplay/airshow_dnssd.c`, so AirPlay starts without dnssd.dll. CI smoke tests browse for `_airplay._tcp` over mDNS
- Still to do: Intel macOS build, notarization/signing, repeat step 2 on each platform

## Known external risks

- **Cast device auth** relies on a precomputed signature table extracted from AirReceiver (via shanocast). It covers 2023-08-15 → ~2027-12-21 and cannot be extended without a newer table. Google can also revoke the device cert or enforce nonce checking at any time.
- **Miracast over Wi-Fi Direct** remains out of scope on Linux/macOS; Android phones that do Miracast only use Wi-Fi Direct (stock Android dropped Miracast), so the AirShow sender is the Android path.
- **Miracast discovery may need Wi-Fi Direct even for MS-MICE.** As understood from the MS-MICE spec, Windows sources discover sinks via Wi-Fi Direct beacons and only then connect over the LAN; AirShow advertises `_display._tcp` over mDNS only. If confirmed on a Windows device, AirShow won't appear in Windows' Connect list (and an Ethernet-only receiver can't be found). Candidate fix for Windows receivers: the OS `Windows.Media.Miracast.MiracastReceiver` API (Windows 10 2004+), which also adds Wi-Fi Direct Miracast.
