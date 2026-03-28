#pragma once
#include <QString>

namespace myairshow {

// QSettings wrapper for application-level settings.
// Requires QCoreApplication::setOrganizationName() and setApplicationName()
// to be called before construction (done in main.cpp).
//
// Uses QSettings::NativeFormat (platform default):
//   Windows: HKCU\Software\MyAirShow\MyAirShow
//   macOS:   ~/Library/Preferences/com.myairshow.MyAirShow.plist
//   Linux:   ~/.config/MyAirShow/MyAirShow.conf
class AppSettings {
public:
    AppSettings();

    // Returns stored receiver name, or system hostname as default (D-10).
    QString receiverName() const;

    // Persist a new receiver name. Caller must then call
    // discoveryManager->rename() to re-register advertisements (D-11).
    void setReceiverName(const QString& name);

    // Returns true if this is the first launch (used for Windows firewall, D-12).
    bool isFirstLaunch() const;

    // Mark first-launch as complete. Call after firewall rules are registered.
    void setFirstLaunchComplete();

private:
    // Returns the platform default receiver name.
    static QString defaultReceiverName();
};

} // namespace myairshow
