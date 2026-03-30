#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include "settings/AppSettings.h"

namespace myairshow {

// QObject bridge that exposes application settings to QML via Q_PROPERTY.
// QML binds to appSettings.receiverName; the value is read from AppSettings
// at startup via setContextProperty("appSettings", settingsBridge).
//
// Phase 3 reads receiver name at startup. Live update via NOTIFY is
// forward-compatible — Phase 7 calls receiverNameChanged() when the user
// saves a new name in the settings panel.
//
// Phase 7 (D-12): Extended with security properties:
//   requireApproval, pinEnabled, pin, trustedDevices, clearTrustedDevices()
class SettingsBridge : public QObject {
    Q_OBJECT

    // --- Receiver identity -----------------------------------------------
    Q_PROPERTY(QString receiverName
               READ receiverName
               NOTIFY receiverNameChanged)

    // --- Security settings (Phase 7 / D-12) ------------------------------
    Q_PROPERTY(bool requireApproval
               READ requireApproval
               WRITE setRequireApproval
               NOTIFY requireApprovalChanged)

    Q_PROPERTY(bool pinEnabled
               READ pinEnabled
               WRITE setPinEnabled
               NOTIFY pinEnabledChanged)

    Q_PROPERTY(QString pin
               READ pin
               WRITE setPin
               NOTIFY pinChanged)

    // Read-only from QML — modifications go through addTrustedDevice /
    // clearTrustedDevices() invokables; updates are broadcast via the signal.
    Q_PROPERTY(QStringList trustedDevices
               READ trustedDevices
               NOTIFY trustedDevicesChanged)

public:
    explicit SettingsBridge(AppSettings& settings, QObject* parent = nullptr);

    // --- Receiver identity -----------------------------------------------
    QString receiverName() const;

    // --- Security settings -----------------------------------------------
    bool    requireApproval() const;
    void    setRequireApproval(bool v);

    bool    pinEnabled() const;
    void    setPinEnabled(bool v);

    QString pin() const;
    void    setPin(const QString& v);

    QStringList trustedDevices() const;

    // Clears the trusted device list and emits trustedDevicesChanged.
    Q_INVOKABLE void clearTrustedDevices();

signals:
    void receiverNameChanged(const QString& receiverName);

    // Security signals
    void requireApprovalChanged(bool requireApproval);
    void pinEnabledChanged(bool pinEnabled);
    void pinChanged(const QString& pin);
    void trustedDevicesChanged(const QStringList& trustedDevices);

private:
    AppSettings& m_settings;
};

} // namespace myairshow
