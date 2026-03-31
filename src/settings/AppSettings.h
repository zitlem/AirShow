#pragma once
#include <QString>
#include <QStringList>

namespace airshow {

// QSettings wrapper for application-level settings.
// Requires QCoreApplication::setOrganizationName() and setApplicationName()
// to be called before construction (done in main.cpp).
//
// Uses QSettings::NativeFormat (platform default):
//   Windows: HKCU\Software\AirShow\AirShow
//   macOS:   ~/Library/Preferences/com.airshow.AirShow.plist
//   Linux:   ~/.config/AirShow/AirShow.conf
class AppSettings {
public:
    AppSettings();

    // --- Receiver identity -----------------------------------------------

    // Returns stored receiver name, or system hostname as default (D-10).
    QString receiverName() const;

    // Persist a new receiver name. Caller must then call
    // discoveryManager->rename() to re-register advertisements (D-11).
    void setReceiverName(const QString& name);

    // Returns true if this is the first launch (used for Windows firewall, D-12).
    bool isFirstLaunch() const;

    // Mark first-launch as complete. Call after firewall rules are registered.
    void setFirstLaunchComplete();

    // --- Security settings (Phase 7 / D-11) ------------------------------

    // Whether incoming connections require explicit user approval (default: true).
    // Key: "security/requireApproval"
    bool requireApproval() const;
    void setRequireApproval(bool v);

    // Whether PIN-based pairing is enabled (default: false).
    // Key: "security/pinEnabled"
    bool pinEnabled() const;
    void setPinEnabled(bool v);

    // The 4-digit PIN string displayed to users for pairing (default: "").
    // Key: "security/pin"
    QString pin() const;
    void setPin(const QString& v);

    // List of stable device identifiers that have been approved for connection
    // without re-prompting (MAC for AirPlay, UUID for Cast, IP+UA for DLNA).
    // Key: "security/trustedDevices"
    QStringList trustedDevices() const;

    // Append deviceId to trusted list if not already present.
    void addTrustedDevice(const QString& deviceId);

    // Remove a specific device from the trusted list.
    void removeTrustedDevice(const QString& deviceId);

    // Clear all trusted devices.
    void clearTrustedDevices();

private:
    // Returns the platform default receiver name.
    static QString defaultReceiverName();
};

} // namespace airshow
