# Phase 1: Foundation - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Build system setup (CMake), GStreamer media pipeline integrated into a Qt Quick/QML window, audio output with mute toggle, and hardware H.264 decode with software fallback. This phase produces a launchable binary on Linux, macOS, and Windows that can render test video/audio — no protocol code, no network code.

</domain>

<decisions>
## Implementation Decisions

### Build System
- **D-01:** Use CMake as the build system with find_package for Qt6 and GStreamer dependencies
- **D-02:** On Linux, depend on system-installed Qt6 and GStreamer packages. On macOS and Windows, bundle dependencies (Homebrew Qt6 + GStreamer framework on macOS; vcpkg or pre-built binaries on Windows)
- **D-03:** C++17 standard — matches research recommendation and AirServer's confirmed stack

### Pipeline Architecture
- **D-04:** Use `qml6glsink` as the GStreamer video sink — renders directly into a QML item via shared GL context. This is the approach confirmed by Qt's own AirServer case study
- **D-05:** Use `appsrc` injection point for feeding protocol data into the pipeline (future phases will push data here). For Phase 1, use `videotestsrc` and `audiotestsrc` to validate the pipeline
- **D-06:** Single shared GStreamer pipeline architecture — all future protocols will converge on one pipeline, not per-protocol pipelines

### Audio Output
- **D-07:** Use GStreamer `autoaudiosink` which auto-selects the correct platform backend (PipeWire/PulseAudio on Linux, CoreAudio on macOS, WASAPI on Windows)
- **D-08:** Mute toggle implemented by setting the audio sink volume to 0 (not by disconnecting the audio branch)

### Window Framework
- **D-09:** Qt Quick/QML for the receiver window — required for qml6glsink integration and provides GPU-accelerated rendering
- **D-10:** Application launches fullscreen by default on the primary display

### Hardware Decode
- **D-11:** Use GStreamer `decodebin` with rank-based decoder selection. Log which decoder (vaapih264dec, nvh264dec, vtdec, d3d11h264dec, avdec_h264) is selected
- **D-12:** If hardware decode fails or is unavailable, fall back to software `avdec_h264` and log a warning — do not crash or refuse to play

### Claude's Discretion
- CMake module structure and directory layout
- Specific CI/CD pipeline configuration (if any in Phase 1)
- Test infrastructure setup choices
- Exact GStreamer pipeline element chain beyond the decisions above

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Stack Research
- `.planning/research/STACK.md` — Prescriptive stack recommendations with versions (Qt 6.8 LTS, GStreamer 1.26.x, C++17)
- `.planning/research/ARCHITECTURE.md` — Component boundaries, data flow, build order recommendations

### Pitfalls
- `.planning/research/PITFALLS.md` — Hardware decode fallback strategy, AV sync requirements, pipeline architecture anti-patterns

### Summary
- `.planning/research/SUMMARY.md` — Synthesized research findings and phase ordering rationale

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- None — greenfield project, no existing code

### Established Patterns
- None — patterns will be established in this phase

### Integration Points
- The GStreamer pipeline's `appsrc` element is the integration point for all future protocol handlers (Phases 4-8)
- The QML window is the integration point for the Display UI phase (Phase 3)

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches. Research recommends the Qt + GStreamer + qml6glsink pattern used by AirServer (confirmed by Qt case study).

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 01-foundation*
*Context gathered: 2026-03-28*
