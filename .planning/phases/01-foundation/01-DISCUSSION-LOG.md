# Phase 1: Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 1-Foundation
**Areas discussed:** Build system, Pipeline architecture, Audio output, Window framework
**Mode:** Auto (all recommended defaults selected)

---

## Build System

| Option | Description | Selected |
|--------|-------------|----------|
| System packages everywhere | Depend on system-installed Qt6 and GStreamer on all platforms | |
| System on Linux, bundled on macOS/Windows | System packages on Linux, bundle dependencies on macOS and Windows | ✓ |
| Vendor everything | Bundle all dependencies on all platforms | |

**User's choice:** System on Linux, bundled on macOS/Windows (auto-selected recommended default)
**Notes:** Matches how UxPlay and similar cross-platform projects handle dependency management. Linux distros have good Qt6/GStreamer packages; macOS and Windows need bundling for reliable user experience.

---

## Pipeline Architecture

| Option | Description | Selected |
|--------|-------------|----------|
| qml6glsink | GStreamer renders directly into QML via shared GL context | ✓ |
| Off-screen render + blit | GStreamer renders to texture, manually blit into Qt widget | |
| gtkglsink + GTK window | Use GTK instead of Qt for the window | |

**User's choice:** qml6glsink (auto-selected recommended default)
**Notes:** Confirmed by Qt's own AirServer case study. Provides zero-copy GPU path from decoder to display.

---

## Audio Output

| Option | Description | Selected |
|--------|-------------|----------|
| autoaudiosink | GStreamer auto-selects platform audio backend | ✓ |
| Platform-specific sinks | Manually select pipewiresink/osxaudiosink/wasapisink per platform | |

**User's choice:** autoaudiosink (auto-selected recommended default)
**Notes:** GStreamer handles platform abstraction natively. autoaudiosink selects PipeWire/PulseAudio on Linux, CoreAudio on macOS, WASAPI on Windows.

---

## Window Framework

| Option | Description | Selected |
|--------|-------------|----------|
| Qt Quick/QML | Required for qml6glsink, GPU-accelerated, declarative UI | ✓ |
| Qt Widgets | Traditional widget toolkit, cannot use qml6glsink | |

**User's choice:** Qt Quick/QML (auto-selected recommended default)
**Notes:** Required for qml6glsink integration. Also provides the GPU-accelerated rendering path needed for smooth video display.

---

## Claude's Discretion

- CMake module structure and directory layout
- CI/CD pipeline configuration
- Test infrastructure setup
- Exact GStreamer pipeline element chain

## Deferred Ideas

None — discussion stayed within phase scope
