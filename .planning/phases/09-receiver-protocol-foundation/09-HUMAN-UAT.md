---
status: partial
phase: 09-receiver-protocol-foundation
source: [09-VERIFICATION.md]
started: 2026-03-31T17:35:00Z
updated: 2026-03-31T17:35:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Live TCP handshake on port 7400
expected: Run AirShow binary, then `echo '{"type":"HELLO","version":1,"deviceName":"Manual Test","codec":"h264","maxResolution":"1920x1080","targetBitrate":4000000,"fps":30}' | nc -q 1 localhost 7400` returns JSON response with type=HELLO_ACK, acceptedResolution, acceptedBitrate, acceptedFps fields
result: [pending]

### 2. mDNS advertisement visible
expected: With the application running, `avahi-browse -t _airshow._tcp` shows AirShow receiver with port 7400
result: [pending]

### 3. CTest suite passes
expected: `cd build && ctest -R AirShowHandler --output-on-failure` — all 3 tests pass
result: passed (verified automatically — 3/3 tests green)

### 4. Live video streaming
expected: With handshake established, streaming 16-byte framed VIDEO_NAL data produces visible video on the receiver display
result: [pending]

## Summary

total: 4
passed: 1
issues: 0
pending: 3
skipped: 0
blocked: 0

## Gaps
