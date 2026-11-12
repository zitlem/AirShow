#pragma once
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <cstdint>
#include <memory>

namespace airshow {

// Makes the receiver show up in Windows "Connect" (Win+K) as a Miracast over
// Infrastructure (MS-MICE) sink.
//
// Windows discovers MS-MICE sinks through Wi-Fi Direct: it only lists a sink
// whose P2P probe responses carry the MS-MICE Vendor Extension (Capability +
// Host Name attributes, [MS-MICE] 2.2.8). It then resolves the host name
// (mDNS _display._tcp, see DiscoveryManager) and connects to TCP 7250 over the
// existing network; no Wi-Fi Direct group is ever formed.
//
// On Linux this asks wpa_supplicant (D-Bus API, fi.w1.wpa_supplicant1) to add
// that Vendor Extension and a Wi-Fi Display IE to its P2P device, and to stay
// in extended listen so Windows' discovery scan gets a response. The station
// connection is not touched. Previous P2P settings are restored on stop().
//
// Needs a Wi-Fi adapter with P2P support and permission to use wpa_supplicant's
// D-Bus API (root, or membership of the "netdev" group on Debian/Ubuntu).
// When unavailable, start() logs why and returns false; Miracast still works
// for sources that already know the receiver's address. No-op elsewhere.
class WifiDirectBeacon {
public:
    WifiDirectBeacon();
    ~WifiDirectBeacon();
    WifiDirectBeacon(const WifiDirectBeacon&) = delete;
    WifiDirectBeacon& operator=(const WifiDirectBeacon&) = delete;

    // deviceName: name shown in Win+K (the receiver name). Returns true if active.
    bool start(const QString& deviceName);
    void stop();
    void rename(const QString& deviceName);
    bool isActive() const { return m_active; }

    // MS-MICE Vendor Extension value (WPS Vendor Extension attribute body):
    // Microsoft vendor ID 00:01:37, then Capability (0x2001) and Host Name (0x2002)
    // attributes. Public for unit tests.
    static QByteArray buildMiceVendorExtension(const QString& hostName);

    // Wi-Fi Display IE subelements for a primary sink available for a session.
    static QByteArray buildWfdIes(uint16_t rtspPort);

private:
    bool apply(bool logFailures);

    struct Impl;
    std::unique_ptr<Impl> d;
    QString m_deviceName;
    bool    m_active = false;
    QTimer  m_refreshTimer;
};

} // namespace airshow
