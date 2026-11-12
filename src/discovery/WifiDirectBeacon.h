#pragma once
#include <QByteArray>
#include <QString>
#include <QThread>
#include <QTimer>
#include <atomic>
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
// connection is not touched.
//
// Restoring wpa_supplicant: the P2P settings in place before start() are
// saved to QSettings ("miracast/wifiDirectRestore/*") and put back by stop().
// If AirShow is killed or crashes before stop(), the next run puts them back
// (recoverStaleState(), or start() reusing the saved values).
//
// D-Bus calls are synchronous with a 2 s timeout, so they run on a worker
// thread and never block the GUI thread. stop() waits for the restore.
//
// Needs a Wi-Fi adapter with P2P support and permission to use wpa_supplicant's
// D-Bus API (root, or membership of the "netdev" group on Debian/Ubuntu).
// When unavailable, it logs why and the receiver simply doesn't appear in
// Win+K; other protocols are unaffected. No-op on macOS and Windows.
class WifiDirectBeacon {
public:
    WifiDirectBeacon();
    ~WifiDirectBeacon();
    WifiDirectBeacon(const WifiDirectBeacon&) = delete;
    WifiDirectBeacon& operator=(const WifiDirectBeacon&) = delete;

    // deviceName: name shown in Win+K (the receiver name). Asynchronous: the
    // outcome is logged, and isActive() reflects it once applied.
    void start(const QString& deviceName);
    // Restores the previous P2P settings. Blocks until done (up to a few seconds
    // if wpa_supplicant is unresponsive). Safe to call when not started.
    void stop();
    void rename(const QString& deviceName);
    bool isActive() const { return m_active.load(); }

    // For when the beacon is disabled: if a previous run was killed while the
    // beacon was on, restore the P2P settings it saved. Asynchronous.
    void recoverStaleState();

    // MS-MICE Vendor Extension value (WPS Vendor Extension attribute body):
    // Microsoft vendor ID 00:01:37, then Capability (0x2001) and Host Name (0x2002)
    // attributes. Public for unit tests.
    static QByteArray buildMiceVendorExtension(const QString& hostName);

    // Wi-Fi Display IE subelements for a primary sink available for a session.
    static QByteArray buildWfdIes(uint16_t rtspPort);

private:
    void ensureWorker();
    void postApply(bool logResult);

    struct Impl;                 // lives on m_thread (Linux)
    std::unique_ptr<Impl> d;
    QThread           m_thread;
    QTimer            m_refreshTimer;
    QString           m_deviceName;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_applyPending{false};
};

} // namespace airshow
