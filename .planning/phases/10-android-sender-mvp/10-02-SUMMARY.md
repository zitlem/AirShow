---
phase: 10-android-sender-mvp
plan: 02
subsystem: android-native
tags: [kotlin, android, mediaprojection, mediacodec, h264, tcp, foreground-service, audiocapture, methodchannel, eventchannel]

requires:
  - phase: 10-android-sender-mvp
    plan: 01
    provides: AirShowChannel Dart contract (MethodChannel + EventChannel) for native Kotlin to implement

provides:
  - AirShowCaptureService foreground service with MediaProjection consent, VirtualDisplay, H264Encoder, audio capture, TCP streaming
  - H264Encoder MediaCodec H.264 wrapper with NAL extraction, SPS/PPS caching, AirShow 16-byte frame header building
  - MainActivity MethodChannel/EventChannel registration with MediaProjection consent flow
  - AndroidManifest with all required permissions and foreground service declaration
  - Debug APK (flutter build apk --debug succeeds)

affects: [10-android-sender-mvp/10-03, 11-ios-sender]

tech-stack:
  added:
    - MediaProjectionManager (Android API 21+) — screen capture consent via onActivityResult
    - VirtualDisplay (Android API 21+) — virtual screen feeding H.264 encoder surface
    - MediaCodec H.264 hardware encoder — surface-mode encoding at 4Mbps/30fps, baseline L3.1
    - AudioPlaybackCaptureConfiguration (Android API 29+) — system audio capture from media/game/unknown apps
    - AudioRecord — PCM 16-bit stereo 44100 Hz buffer reader
    - NotificationCompat + PendingIntent — foreground notification with Stop action button
    - java.net.Socket — TCP client connecting to AirShow receiver on port 7400
    - org.json.JSONObject — HELLO/HELLO_ACK handshake serialization
  patterns:
    - SPS/PPS prepend pattern: BUFFER_FLAG_CODEC_CONFIG cached; prepended to every BUFFER_FLAG_KEY_FRAME
    - AirShow 16-byte frame header: type(1B) | flags(1B) | length(4B) | ptsNs(8B) | reserved(2B) big-endian
    - onActivityResult (not registerForActivityResult) for FlutterActivity — FlutterActivity does not extend ComponentActivity
    - synchronized(outputStream) for concurrent video + audio thread writes
    - postEvent via Handler(mainLooper).post — EventSink.success must run on main thread

key-files:
  created:
    - sender/android/app/src/main/kotlin/com/airshow/airshow_sender/AirShowCaptureService.kt
    - sender/android/app/src/main/kotlin/com/airshow/airshow_sender/H264Encoder.kt
  modified:
    - sender/android/app/src/main/kotlin/com/airshow/airshow_sender/MainActivity.kt
    - sender/android/app/src/main/AndroidManifest.xml

key-decisions:
  - "onActivityResult (deprecated) is correct for FlutterActivity — registerForActivityResult requires ComponentActivity which FlutterActivity does not directly provide; deprecated suppression applied"
  - "Resolution clamping to 1280x720 with 16-pixel alignment: maxOf(newW, 16) ensures encoder never gets zero dimensions on very small screens"
  - "SPS/PPS cached from BUFFER_FLAG_CODEC_CONFIG and prepended to every IDR frame so receiver decodes any keyframe standalone (no separate SPS/PPS negotiation needed)"
  - "AudioPlaybackCapture wrapped in try-catch: audio capture is best-effort — pipeline continues if audio fails (e.g., device does not support API 29)"

requirements-completed: [SEND-01, SEND-05]

duration: ~188min
completed: 2026-04-02
---

# Phase 10 Plan 02: Native Android Kotlin Pipeline Summary

**MediaProjection consent, foreground service, H.264 MediaCodec encoding, AudioPlaybackCapture, TCP streaming with AirShow 16-byte framing, JSON HELLO/HELLO_ACK handshake, and EventChannel events — debug APK builds successfully**

## Performance

- **Duration:** ~188 min (includes Android SDK installation and build-time troubleshooting)
- **Started:** 2026-04-01T23:38:37Z
- **Completed:** 2026-04-02T02:47:22Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Full native capture-encode-stream pipeline in Kotlin — no frame data crosses to Dart
- H264Encoder wraps MediaCodec in async callback mode: hardware encoder preferred, falls back to any AVC encoder
- SPS/PPS config frames cached from BUFFER_FLAG_CODEC_CONFIG and prepended to each IDR frame — receiver can decode standalone keyframes without out-of-band parameter exchange
- AirShowCaptureService owns all pipeline resources: MediaProjection, VirtualDisplay, H264Encoder, AudioRecord, and TCP socket
- JSON handshake (HELLO/HELLO_ACK) with receiver before streaming begins; streams 16-byte framed NAL units to port 7400
- AudioPlaybackCapture (API 29+) captures system audio as TYPE_AUDIO (0x02) frames concurrently with video
- Foreground notification with Stop button and FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION (required Android 14+)
- EventChannel posts CONNECTED, DISCONNECTED, ERROR events back to Dart via MainActivity.eventSink
- Resolution clamped to 1280x720 max with aspect-ratio preservation and 16-pixel alignment for encoder compatibility
- Android SDK 35 installed to ~/android-sdk; `flutter build apk --debug` succeeds

## Task Commits

1. **Task 1: AndroidManifest permissions + MainActivity MethodChannel/EventChannel** — `fd34f27`
2. **Task 2: AirShowCaptureService + H264Encoder** — `4bfe5a4`

## Files Created/Modified

- `sender/android/app/src/main/AndroidManifest.xml` — Added INTERNET, FOREGROUND_SERVICE, FOREGROUND_SERVICE_MEDIA_PROJECTION, RECORD_AUDIO, POST_NOTIFICATIONS permissions; registered AirShowCaptureService with foregroundServiceType="mediaProjection"
- `sender/android/app/src/main/kotlin/com/airshow/airshow_sender/MainActivity.kt` — MethodChannel + EventChannel registration, MediaProjection consent via onActivityResult, shared eventSink companion field
- `sender/android/app/src/main/kotlin/com/airshow/airshow_sender/AirShowCaptureService.kt` — 397 lines: complete foreground service with MediaProjection, VirtualDisplay, H264Encoder, AudioPlaybackCapture, TCP socket, HELLO handshake, teardown
- `sender/android/app/src/main/kotlin/com/airshow/airshow_sender/H264Encoder.kt` — 193 lines: MediaCodec wrapper, SPS/PPS caching, 16-byte frame header builder

## Decisions Made

- `onActivityResult` (deprecated) is the correct API for `FlutterActivity` — `registerForActivityResult` requires `ComponentActivity`, which `FlutterActivity` does not directly extend in this Flutter version
- 16-pixel alignment in resolution clamping ensures H.264 encoder macroblock alignment (most hardware encoders require width/height divisible by 16)
- SPS/PPS prepended to every IDR frame (not just the first): receiver can join a stream mid-session and still decode

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed MainActivity compilation error: registerForActivityResult not available on FlutterActivity**

- **Found during:** Task 2 (first build attempt)
- **Issue:** Plan specified `registerForActivityResult(ActivityResultContracts.StartActivityForResult())` but `FlutterActivity` does not extend `ComponentActivity` directly, causing "Unresolved reference: registerForActivityResult" compilation errors
- **Fix:** Replaced with `startActivityForResult` + `onActivityResult` override (the traditional Android approach, with `@Suppress("OVERRIDE_DEPRECATION", "DEPRECATION")` annotations)
- **Files modified:** `sender/android/app/src/main/kotlin/com/airshow/airshow_sender/MainActivity.kt`
- **Commit:** fd34f27 (amended behavior, same commit)

**2. [Rule 3 - Blocking] Android SDK installation: --sdk_root required for sdkmanager**

- **Found during:** Task 1 (Android SDK install)
- **Issue:** First `sdkmanager` invocation ran `--licenses` without `--sdk_root`, accepting licenses for the default SDK location instead of `~/android-sdk`; subsequent `sdkmanager` install commands placed packages in wrong location leaving `~/android-sdk/platforms` and `~/android-sdk/build-tools` missing
- **Fix:** Re-ran `sdkmanager --sdk_root=/home/sanya/android-sdk --licenses` then `sdkmanager --sdk_root=/home/sanya/android-sdk "platform-tools" "platforms;android-35" "build-tools;35.0.0"`
- **Impact:** No code changes; build unblocked

---

**Total deviations:** 2 auto-fixed (Rule 1 - Bug, Rule 3 - Blocking)

## Known Stubs

None — all acceptance criteria met. The native pipeline is wired end-to-end; no placeholder returns or hardcoded empty values.

## Issues Encountered

- Flutter Gradle plugin auto-installed CMake 3.22.1 during first successful build — this is expected behavior and does not affect the output
- `flutter_foreground_task` from Plan 01 is in `pubspec.yaml` but not used in this plan's Kotlin implementation (Plan 01 included it for future reference; this plan implements the foreground service directly using Android APIs)

## Next Phase Readiness

- Plan 03 (final wiring + APK integration test) can proceed
- The complete native pipeline is in place: Dart sends `startCapture(host, port)` → Kotlin launches `AirShowCaptureService` → connects to receiver → streams H.264 + audio → sends events back
- `flutter build apk --debug` verified green

## Self-Check: PASSED
