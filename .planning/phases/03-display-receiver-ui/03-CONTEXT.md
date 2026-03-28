# Phase 3: Display & Receiver UI - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Fullscreen display with correct aspect ratio letterboxing, connection status HUD overlay, and idle/waiting screen. This phase transforms the raw GStreamer test-pattern window into a polished receiver experience. No protocol logic — only display and UI state management.

</domain>

<decisions>
## Implementation Decisions

### Aspect Ratio (DISP-01)
- **D-01:** Letterbox with black bars — preserve the source aspect ratio, never stretch or crop
- **D-02:** The `GstGLQt6VideoItem` already fills the parent in `main.qml`. Implement letterboxing by setting the item's `fillMode` to preserve aspect ratio (or wrapping in a container that centers it with black margins)
- **D-03:** The window background remains black (`color: "black"` already set in main.qml), providing natural letterbox bars

### Connection HUD (DISP-02)
- **D-04:** Semi-transparent overlay showing device name and protocol in use (e.g., "iPhone 15 via AirPlay")
- **D-05:** HUD auto-hides after 3 seconds of no state change, reappears on new connection or disconnection
- **D-06:** Position at the top of the screen, horizontally centered, with subtle fade-in/fade-out animation
- **D-07:** Show protocol icon (small) next to device name — use simple text/emoji indicators, not image assets

### Idle Screen (DISP-03)
- **D-08:** Dark background (black) with app name "MyAirShow", receiver name (from AppSettings), and "Waiting for connection..." text
- **D-09:** Idle screen is visible when no mirroring session is active. Hides when content starts, reappears when session ends
- **D-10:** Receiver name updates live if changed in settings (bind to AppSettings.receiverName)
- **D-11:** Minimal animation — subtle opacity pulse on the "Waiting..." text to show the app is alive

### Visual Style
- **D-12:** Dark/translucent overlays (semi-transparent black background, ~70% opacity) with white text
- **D-13:** Font: system default sans-serif, clean and readable at TV-viewing distance
- **D-14:** Subtle opacity/fade animations (200-300ms duration), no bouncing or sliding
- **D-15:** No heavy theming or custom design system — keep it minimal and TV-receiver-like

### Claude's Discretion
- Exact QML component structure and file organization
- Whether to split into multiple QML files or keep in main.qml
- Exact animation timing and easing curves
- Whether mute button styling needs updating to match new visual style

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing UI Code
- `qml/main.qml` — Current QML window with GstGLQt6VideoItem and mute button
- `src/ui/ReceiverWindow.h` — Window manager that loads QML and wires pipeline
- `src/ui/ReceiverWindow.cpp` — Implementation with findChild("videoItem") pattern
- `src/ui/AudioBridge.h` — QObject bridge for mute toggle (context property "audioBridge")

### Settings Integration
- `src/settings/AppSettings.h` — receiverName() and nameChanged signal for live binding
- `src/discovery/DiscoveryManager.h` — Connection state source (future integration point)

### Phase 1 Context
- `.planning/phases/01-foundation/01-CONTEXT.md` — D-09 (Qt Quick/QML), D-10 (fullscreen default)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `GstGLQt6VideoItem` in main.qml — video rendering target, already fullscreen
- `AudioBridge` context property — mute toggle already wired
- `AppSettings` — receiver name with `nameChanged` signal for QML binding
- `ReceiverWindow` — manages QML engine and context properties

### Established Patterns
- QML with Qt Quick Controls for UI
- Context properties for C++ → QML bridges (audioBridge pattern)
- findChild for QML → C++ references (videoItem pattern)

### Integration Points
- `main.qml` — primary modification target for all UI changes
- `ReceiverWindow.cpp` — expose new context properties (connection state, idle state)
- `AppSettings` — already provides receiverName for idle screen display

</code_context>

<specifics>
## Specific Ideas

- Idle screen should feel like an Apple TV or Chromecast waiting screen — dark, minimal, informative
- Connection HUD should feel like a brief notification, not a permanent fixture
- The mute button from Phase 1 should remain but may need visual refinement to match the new overlay style

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 03-display-receiver-ui*
*Context gathered: 2026-03-28*
