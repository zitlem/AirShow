#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace myairshow {

struct TxtRecord {
    std::string key;
    std::string value;
};

// Platform-agnostic mDNS/DNS-SD service advertisement interface.
// Concrete implementations: AvahiAdvertiser (Linux), DnsSdAdvertiser (macOS),
// BonjourAdvertiser (Windows). Use ServiceAdvertiser::create() to get the
// correct backend for the current platform.
class ServiceAdvertiser {
public:
    virtual ~ServiceAdvertiser() = default;

    // Advertise a DNS-SD service. Returns false if advertisement fails.
    // serviceType example: "_airplay._tcp"
    virtual bool advertise(const std::string& serviceType,
                           const std::string& name,
                           uint16_t port,
                           const std::vector<TxtRecord>& txt) = 0;

    // Re-register all active advertisements with a new name (D-11).
    // Called immediately when the user changes the receiver name.
    virtual bool rename(const std::string& newName) = 0;

    // Stop all advertisements cleanly.
    virtual void stop() = 0;

    // Update a single TXT record key for an already-advertised service.
    // Used by AirPlayHandler to replace the placeholder pk value with the real Ed25519 public key.
    // serviceType: e.g., "_airplay._tcp"
    // key: TXT record key to update (e.g., "pk")
    // value: new value for that key
    // Returns false if the service type is not found or the update fails.
    virtual bool updateTxtRecord(const std::string& serviceType,
                                 const std::string& key,
                                 const std::string& value) = 0;

    // Factory: returns the correct backend for the current platform.
    // Linux: AvahiAdvertiser, macOS: DnsSdAdvertiser, Windows: BonjourAdvertiser
    static std::unique_ptr<ServiceAdvertiser> create();
};

} // namespace myairshow
