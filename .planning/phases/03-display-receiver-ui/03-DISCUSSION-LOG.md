# Phase 3: Display & Receiver UI - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.

**Date:** 2026-03-28
**Phase:** 3-Display & Receiver UI
**Areas discussed:** Aspect ratio handling, Connection HUD, Idle screen design, Visual style
**Mode:** Auto (all recommended defaults selected)

---

## Aspect Ratio Handling

| Option | Description | Selected |
|--------|-------------|----------|
| Letterbox with black bars | Preserve source aspect ratio, black margins | ✓ |
| Fill and crop | Fill screen, crop edges | |
| Stretch to fit | Distort to fill | |

**User's choice:** Letterbox (auto-selected recommended default)

---

## Connection HUD

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-hiding overlay | Device name + protocol, fades after 3s | ✓ |
| Persistent status bar | Always visible at top/bottom | |
| No HUD | Status in title bar only | |

**User's choice:** Auto-hiding overlay (auto-selected recommended default)

---

## Idle Screen Design

| Option | Description | Selected |
|--------|-------------|----------|
| Dark with app/receiver name | Black bg, "MyAirShow", receiver name, "Waiting..." | ✓ |
| Screensaver-style | Animated logo bouncing/floating | |
| Blank black | Just black screen when idle | |

**User's choice:** Dark with app/receiver name (auto-selected recommended default)

---

## Visual Style

| Option | Description | Selected |
|--------|-------------|----------|
| Dark/translucent minimal | Semi-transparent overlays, white text, subtle fades | ✓ |
| Light/material | Material Design inspired overlays | |
| Custom branded | Heavy theming with custom colors | |

**User's choice:** Dark/translucent minimal (auto-selected recommended default)

---

## Claude's Discretion

- QML component structure
- Animation timing/easing
- Mute button styling updates

## Deferred Ideas

None
