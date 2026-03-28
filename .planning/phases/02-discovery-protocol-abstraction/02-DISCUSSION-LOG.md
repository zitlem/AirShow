# Phase 2: Discovery & Protocol Abstraction - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 2-Discovery & Protocol Abstraction
**Areas discussed:** mDNS library, Protocol handler interface, Receiver name storage, Firewall strategy
**Mode:** Auto (all recommended defaults selected)

---

## mDNS Library

| Option | Description | Selected |
|--------|-------------|----------|
| Platform-native with abstraction | Avahi on Linux, dns_sd on macOS, Bonjour SDK on Windows | ✓ |
| Cross-platform mdns.h | Single-file header-only library | |
| Qt Network (QMdnsEngine) | Qt-native but less mature | |

**User's choice:** Platform-native with thin abstraction layer (auto-selected recommended default)
**Notes:** Most reliable discovery — platform-native implementations are what actual AirPlay/Cast devices use.

---

## Protocol Handler Interface

| Option | Description | Selected |
|--------|-------------|----------|
| Abstract base class with appsrc feed | start/stop/name/isRunning, feeds shared pipeline | ✓ |
| Plugin system with dynamic loading | .so/.dll plugins loaded at runtime | |
| Callback-based (no inheritance) | Function pointers registered with ProtocolManager | |

**User's choice:** Abstract C++ base class (auto-selected recommended default)
**Notes:** Matches Phase 1 D-06 single shared pipeline. Simple, testable, no dynamic loading complexity.

---

## Receiver Name Storage

| Option | Description | Selected |
|--------|-------------|----------|
| QSettings | Platform-native storage (registry/plist/config) | ✓ |
| JSON config file | Custom config file in app directory | |
| Environment variable | Set via env var | |

**User's choice:** QSettings (auto-selected recommended default)
**Notes:** Qt built-in, cross-platform, no custom serialization needed.

---

## Firewall Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Runtime COM API on first launch | INetFwPolicy2, fallback prompt if no elevation | ✓ |
| Installer-time registration | NSIS/WiX installer adds rules | |
| Manual instructions only | Document which ports to open | |

**User's choice:** Runtime API with fallback prompt (auto-selected recommended default)
**Notes:** No installer dependency. Works for portable/unzipped distributions too.

---

## Claude's Discretion

- Exact TXT record values for service advertisements
- UPnP device description XML
- ServiceAdvertiser abstraction design
- Threading model for discovery services

## Deferred Ideas

None — discussion stayed within phase scope
