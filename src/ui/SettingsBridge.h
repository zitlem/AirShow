#pragma once
#include <QObject>
#include <QString>
#include "settings/AppSettings.h"

namespace myairshow {

// QObject bridge that exposes application settings to QML via Q_PROPERTY.
// QML binds to appSettings.receiverName; the value is read from AppSettings
// at startup via setContextProperty("appSettings", settingsBridge).
//
// Phase 3 only reads the receiver name at startup. Live update via NOTIFY is
// forward-compatible — Phase 7 will call receiverNameChanged() when the user
// saves a new name in the settings panel. For now setContextProperty is called
// once before engine.load().
//
// Per D-10: receiver name updates live if changed in settings. The bridge
// structure is ready for Phase 7; Phase 3 just sets it at startup.
class SettingsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString receiverName READ receiverName NOTIFY receiverNameChanged)

public:
    explicit SettingsBridge(AppSettings& settings, QObject* parent = nullptr);

    // Delegates to m_settings.receiverName().
    QString receiverName() const;

signals:
    void receiverNameChanged(const QString& receiverName);

private:
    AppSettings& m_settings;
};

} // namespace myairshow
