---
phase: 07-security-hardening
plan: 02
subsystem: security
tags: [qt6, security, rfc1918, airplay, dlna, cast, qsemaphore, async-approval, pin-pairing]

requires:
  - phase: 07-security-hardening plan 01
    provides: SecurityManager class with checkConnection/checkConnectionAsync/isLocalNetwork API

provides:
  - AirPlayHandler with SecurityManager integration: checkConnection() in onReportClientRequest, PIN pairing via raop_set_plist + display_pin trampoline
  - DlnaHandler with SecurityManager integration: isLocalNetwork() RFC1918 filter + checkConnection() in handleSoapAction
  - CastHandler with SecurityManager integration: isLocalNetwork() RFC1918 filter + checkConnectionAsync() in onPendingConnection
  - main.cpp: SecurityManager construction, requestApproval -> showApprovalRequest wiring, securityManager QML context property, setSecurityManager() calls on all three handlers
  - SecurityManager.cpp in CMakeLists.txt build sources (added by concurrent Plan 03 commit)

affects:
  - 07-03 (QML approval dialog reads ConnectionBridge approval props set by SecurityManager signal)

tech-stack:
  added:
    - raop_cb_display_pin trampoline (UxPlay PIN pairing C callback)
    - raop_set_plist("pin", value) after raop_init2 for PIN mode activation
    - QCryptographicHash::Sha256 for Cast TLS peer cert fingerprint as device ID
    - QPointer<QSslSocket> in CastHandler for safe socket lifetime tracking during async approval
    - QQmlContext::setContextProperty for securityManager QML exposure
  patterns:
    - AirPlay/DLNA: synchronous checkConnection (QSemaphore) from protocol callback threads (non-Qt)
    - Cast: async checkConnectionAsync from Qt main thread (avoids event loop deadlock, Pitfall 1)
    - RFC1918 guard via SecurityManager::isLocalNetwork() at connection entry point in all protocols
    - SecurityManager wired to ConnectionBridge via QObject::connect (Qt signal/slot, thread-safe)

key-files:
  created: []
  modified:
    - src/protocol/AirPlayHandler.h
    - src/protocol/AirPlayHandler.cpp
    - src/protocol/DlnaHandler.h
    - src/protocol/DlnaHandler.cpp
    - src/protocol/CastHandler.h
    - src/protocol/CastHandler.cpp
    - src/main.cpp
    - src/ui/ReceiverWindow.h
    - tests/CMakeLists.txt
    - CMakeLists.txt

key-decisions:
  - "AirPlay uses synchronous checkConnection (RAOP callback runs on UxPlay non-Qt thread) — async would require posting to Qt thread which is safe but unnecessary; sync blocks the RAOP thread which is acceptable"
  - "DLNA uses synchronous checkConnection (libupnp thread pool, not Qt main thread) — same rationale as AirPlay"
  - "Cast uses checkConnectionAsync (Qt main thread in onPendingConnection) — blocking here would deadlock the Qt event loop (RESEARCH.md Pitfall 1)"
  - "display_pin callback is a no-op in MyAirShow: PIN is pre-configured via AppSettings and shown statically by IdleScreen.qml binding; UxPlay's runtime callback is redundant"
  - "QPointer<QSslSocket> used in CastHandler to detect socket deletion between approval request and callback delivery"
  - "ReceiverWindow::engine() accessor added to expose QQmlApplicationEngine for context property injection"
  - "test_airplay and test_dlna CMakeLists updated to include SecurityManager.cpp + Qt6::Network (required after AirPlayHandler/DlnaHandler gained SecurityManager dependency)"

patterns-established:
  - "Pattern: SecurityManager wiring — construct after AppSettings, connect requestApproval to ConnectionBridge::showApprovalRequest, expose as QML context property, then call setSecurityManager() on each handler before addHandler()"
  - "Pattern: CastHandler async approval — store socket in QPointer, call checkConnectionAsync, create CastSession in callback only if socket still valid"

requirements-completed:
  - SEC-01
  - SEC-02
  - SEC-03

duration: ~10min
completed: 2026-03-30
---

# Phase 7 Plan 02: SecurityManager Protocol Handler Integration Summary

**SecurityManager wired into AirPlay, DLNA, and Cast protocol handlers; main.cpp constructs and connects SecurityManager to all handlers and the QML engine**

## Performance

- **Duration:** ~10 min
- **Completed:** 2026-03-30
- **Tasks:** 2
- **Files modified:** 9 (plus tests/CMakeLists.txt fix)

## Accomplishments

- AirPlayHandler: `setSecurityManager()`, `checkConnection()` in `onReportClientRequest` (synchronous, RAOP thread), `raop_cb_display_pin` trampoline + `raop_set_plist("pin",...)` for PIN pairing when enabled
- DlnaHandler: `setSecurityManager()`, `SecurityManager::isLocalNetwork()` RFC1918 check + `checkConnection()` in `handleSoapAction` using `UpnpActionRequest_get_CtrlPtIPAddr` sockaddr_storage extraction
- CastHandler: `setSecurityManager()`, `isLocalNetwork()` check + `checkConnectionAsync()` in `onPendingConnection` (non-blocking, Qt main thread safe), `QPointer<QSslSocket>` for safe async socket lifetime tracking
- main.cpp: `SecurityManager securityManager(settings)` construction, `requestApproval` signal connected to `ConnectionBridge::showApprovalRequest`, `securityManager` exposed as QML context property, `setSecurityManager()` called on all three handlers
- ReceiverWindow: `engine()` public accessor added to support context property injection from main.cpp
- tests/CMakeLists.txt: `SecurityManager.cpp` + `Qt6::Network` added to `test_airplay`, `test_dlna`, `test_cast` (Rule 3 auto-fix — blocking compile error after SecurityManager dependency added)

## Task Commits

1. **Task 1: AirPlay + DLNA SecurityManager integration** - `1b382f1`
2. **Task 2: Cast SecurityManager integration + main.cpp wiring** - `8be880b`

## Files Created/Modified

- `src/protocol/AirPlayHandler.h` — SecurityManager forward decl, setSecurityManager(), onDisplayPin(), m_securityManager member
- `src/protocol/AirPlayHandler.cpp` — raop_cb_display_pin trampoline, setSecurityManager() impl, raop_set_plist PIN activation, checkConnection() in onReportClientRequest
- `src/protocol/DlnaHandler.h` — SecurityManager forward decl, setSecurityManager(), m_securityManager member
- `src/protocol/DlnaHandler.cpp` — setSecurityManager() impl, isLocalNetwork() + checkConnection() in handleSoapAction; sockaddr_storage IP extraction
- `src/protocol/CastHandler.h` — SecurityManager forward decl, QSslSocket fwd decl, QPointer include, setSecurityManager(), m_securityManager + m_pendingSocket members
- `src/protocol/CastHandler.cpp` — setSecurityManager() impl, isLocalNetwork() check + checkConnectionAsync() in onPendingConnection
- `src/main.cpp` — SecurityManager construction, requestApproval signal wiring, QML context property, setSecurityManager() on all handlers
- `src/ui/ReceiverWindow.h` — engine() accessor added
- `tests/CMakeLists.txt` — SecurityManager.cpp + Qt6::Network added to test_airplay, test_dlna, test_cast

## Decisions Made

- AirPlay and DLNA use synchronous `checkConnection()` — their callbacks run on non-Qt protocol threads where blocking is acceptable
- Cast uses `checkConnectionAsync()` — its callback runs on the Qt main thread; blocking would deadlock the event loop
- `display_pin` callback is effectively a no-op since PIN display is driven by QML bindings to AppSettings, not UxPlay runtime callbacks
- `QPointer<QSslSocket>` used to safely detect socket deletion between checkConnectionAsync call and callback delivery
- ReceiverWindow needed an `engine()` accessor since QQmlApplicationEngine was private — added as minimal public getter

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] test_airplay and test_dlna missing Qt6::Network after SecurityManager dependency added**
- **Found during:** Task 1 (build)
- **Issue:** test_airplay linked Qt6::Core only; after AirPlayHandler gained `#include "security/SecurityManager.h"`, the test target failed to find `QHostAddress` (which SecurityManager.h includes via Qt6::Network)
- **Fix:** Added `SecurityManager.cpp` + `Qt6::Network` to test_airplay, test_dlna, and test_cast in tests/CMakeLists.txt; also added `AppSettings.cpp` to test_dlna (SecurityManager depends on it)
- **Files modified:** tests/CMakeLists.txt
- **Commit:** 1b382f1

**2. [Rule 3 - Blocking] main.cpp missing QQmlContext and ConnectionBridge includes**
- **Found during:** Task 2 (build)
- **Issue:** `window.engine()->rootContext()->setContextProperty(...)` required `QQmlContext` include and `ConnectionBridge.h` for the connect() call
- **Fix:** Added `#include <QQmlContext>` and `#include "ui/ConnectionBridge.h"` to main.cpp
- **Files modified:** src/main.cpp
- **Commit:** 8be880b

**3. [Rule 3 - Blocking] ReceiverWindow::engine() accessor missing**
- **Found during:** Task 2 (build)
- **Issue:** `window.engine()` didn't exist — QQmlApplicationEngine was private
- **Fix:** Added `QQmlApplicationEngine* engine() { return &m_engine; }` public accessor to ReceiverWindow.h
- **Files modified:** src/ui/ReceiverWindow.h
- **Commit:** 8be880b

---

**Total deviations:** 3 auto-fixed (Rule 3 — blocking build errors)

## Pre-existing Test Failure (Out of Scope)

- `PipelineTest.test_video_pipeline` fails in the test environment (GStreamer pipeline doesn't reach PLAYING state in headless CI). This failure predates Plan 07-02 and is unrelated to security changes (confirmed by checking out the pre-07-02 state — same failure). Deferred to `deferred-items.md`.

## Known Stubs

None — all security integration is fully wired. No placeholder data flows to UI.

## Self-Check: PASSED

Files verified:
- `src/protocol/AirPlayHandler.h` — FOUND, contains setSecurityManager() + m_securityManager
- `src/protocol/AirPlayHandler.cpp` — FOUND, contains checkConnection() in onReportClientRequest
- `src/protocol/DlnaHandler.h` — FOUND, contains setSecurityManager() + m_securityManager
- `src/protocol/DlnaHandler.cpp` — FOUND, contains SecurityManager::isLocalNetwork + checkConnection
- `src/protocol/CastHandler.h` — FOUND, contains setSecurityManager() + m_securityManager + m_pendingSocket
- `src/protocol/CastHandler.cpp` — FOUND, contains checkConnectionAsync
- `src/main.cpp` — FOUND, contains SecurityManager construction and wiring
- `src/ui/ReceiverWindow.h` — FOUND, contains engine() accessor
- Commits 1b382f1 and 8be880b — FOUND in git log

---
*Phase: 07-security-hardening*
*Completed: 2026-03-30*
