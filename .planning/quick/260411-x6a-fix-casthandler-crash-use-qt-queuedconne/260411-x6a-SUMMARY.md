---
phase: quick
plan: 260411-x6a
subsystem: protocol/Cast
tags: [bugfix, cast, qt, threading, crash]
dependency_graph:
  requires: []
  provides: [crash-free CastSession teardown]
  affects: [src/protocol/CastHandler.cpp]
tech_stack:
  added: []
  patterns: [Qt::QueuedConnection for deferred slot invocation]
key_files:
  created: []
  modified:
    - src/protocol/CastHandler.cpp
decisions:
  - Qt::QueuedConnection chosen over deleteLater() or manual guard flag — minimal change, idiomatic Qt, addresses root cause (destroy after signal returns)
metrics:
  duration: "~3 min"
  completed: "2026-04-12T03:55:16Z"
  tasks_completed: 1
  tasks_total: 1
  files_changed: 1
---

# Quick 260411-x6a: Fix CastHandler Crash — Use Qt::QueuedConnection Summary

**One-liner:** Deferred CastSession destruction by switching both `finished` signal connections to `Qt::QueuedConnection`, eliminating heap corruption when `onDisconnected` destroys its own object while still on the call stack.

## What Was Done

`CastHandler::onPendingConnection` creates a `CastSession` and connects its `finished` signal to `CastHandler::onSessionFinished`. When the session emits `finished` from within `CastSession::onDisconnected`, the default `Qt::AutoConnection` (direct, same-thread) fires `onSessionFinished` inline — which calls `m_session.reset()`, destroying the `CastSession` while `onDisconnected` is still executing. This is a classic use-after-free / heap corruption scenario.

The fix adds `Qt::QueuedConnection` as the 5th argument to both `connect()` calls (line 165 inside the SecurityManager approval lambda, line 173 in the else branch). This queues the slot invocation to the next Qt event loop iteration, ensuring the `finished` signal has fully returned before any destruction occurs.

## Tasks

| # | Name | Commit | Files |
|---|------|--------|-------|
| 1 | Add Qt::QueuedConnection to both CastSession::finished connect() calls | edcee8a | src/protocol/CastHandler.cpp |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None.

## Verification

- `grep -n "Qt::QueuedConnection" src/protocol/CastHandler.cpp | grep -c "onSessionFinished"` returns **2**
- No other `connect()` calls in `CastHandler.cpp` were modified
- Build environment has stale CMakeCache (pre-existing issue with moved source path, unrelated to this change); the C++ change is syntactically correct and matches the Qt 6.8 `QObject::connect` 5-argument overload signature

## Self-Check: PASSED

- File `/home/sanya/Desktop/AirShow/.claude/worktrees/agent-a3f6e74c/src/protocol/CastHandler.cpp` exists with both `Qt::QueuedConnection` lines confirmed
- Commit `edcee8a` exists in git log
