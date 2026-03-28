#pragma once

namespace myairshow {

// Windows Firewall rule registration for first-launch discovery setup (D-12, D-13).
// On Linux and macOS (D-14): all methods are no-ops that return true immediately.
// On Windows: uses INetFwPolicy2 COM API to register inbound allow rules.
//
// Ports registered:
//   UDP 5353  — mDNS (avahi/Bonjour)
//   UDP 1900  — SSDP (DLNA discovery)
//   TCP 7000  — AirPlay mirroring
//   TCP 8009  — Google Cast
class WindowsFirewall {
public:
    // Attempt to register all required firewall rules.
    // Returns true if rules were registered or already exist.
    // Returns false if permission was denied (D-13: caller must show user prompt).
    static bool registerRules();

    // Returns true if the firewall rules are already registered.
    // Used to skip the UAC prompt on subsequent launches.
    static bool rulesAlreadyRegistered();
};

} // namespace myairshow
