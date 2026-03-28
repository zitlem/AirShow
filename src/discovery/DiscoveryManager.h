#pragma once
#include <memory>
#include <string>

namespace myairshow {

class ServiceAdvertiser;
class AppSettings;

// Owns the ServiceAdvertiser backend and registers all three service records:
//   _airplay._tcp    (DISC-01)
//   _raop._tcp       (DISC-01 — iOS audio/legacy mirroring discovery)
//   _googlecast._tcp (DISC-02)
// DLNA/SSDP is handled by UpnpAdvertiser (Plan 03, separate class).
class DiscoveryManager {
public:
    // settings must outlive DiscoveryManager.
    explicit DiscoveryManager(AppSettings* settings);
    ~DiscoveryManager();

    // Start all service advertisements. Returns false if advertiser unavailable.
    bool start();

    // Stop all service advertisements.
    void stop();

    // Re-register all services with the new name (D-11).
    // Called after AppSettings::setReceiverName().
    void rename(const std::string& newName);

    bool isRunning() const;

private:
    AppSettings*                     m_settings;
    std::unique_ptr<ServiceAdvertiser> m_advertiser;
    bool                             m_running = false;

    // Read device MAC address of first non-loopback NIC for AirPlay deviceid TXT field.
    static std::string readMacAddress();

    // Return or generate a stable UUID stored in QSettings under the given key.
    static std::string getOrCreateUuid(const std::string& settingsKey);
};

} // namespace myairshow
