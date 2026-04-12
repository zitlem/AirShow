#include "settings/AppSettings.h"
#include <QSettings>
#include <QSysInfo>

namespace airshow {

AppSettings::AppSettings() = default;

// ---------------------------------------------------------------------------
// Receiver identity
// ---------------------------------------------------------------------------

QString AppSettings::receiverName() const {
    QSettings s;
    return s.value(QStringLiteral("receiver/name"), defaultReceiverName()).toString();
}

void AppSettings::setReceiverName(const QString& name) {
    QSettings s;
    s.setValue(QStringLiteral("receiver/name"), name);
    s.sync();
}

bool AppSettings::isFirstLaunch() const {
    QSettings s;
    return !s.value(QStringLiteral("app/firstLaunchComplete"), false).toBool();
}

void AppSettings::setFirstLaunchComplete() {
    QSettings s;
    s.setValue(QStringLiteral("app/firstLaunchComplete"), true);
    s.sync();
}

QString AppSettings::defaultReceiverName() {
    QString host = QSysInfo::machineHostName();
    return host.isEmpty() ? QStringLiteral("AirShow") : host;
}

// ---------------------------------------------------------------------------
// Security settings (Phase 7 / D-11)
// ---------------------------------------------------------------------------

bool AppSettings::requireApproval() const {
    QSettings s;
    return s.value(QStringLiteral("security/requireApproval"), false).toBool();
}

void AppSettings::setRequireApproval(bool v) {
    QSettings s;
    s.setValue(QStringLiteral("security/requireApproval"), v);
    s.sync();
}

bool AppSettings::pinEnabled() const {
    QSettings s;
    return s.value(QStringLiteral("security/pinEnabled"), false).toBool();
}

void AppSettings::setPinEnabled(bool v) {
    QSettings s;
    s.setValue(QStringLiteral("security/pinEnabled"), v);
    s.sync();
}

QString AppSettings::pin() const {
    QSettings s;
    return s.value(QStringLiteral("security/pin"), QString()).toString();
}

void AppSettings::setPin(const QString& v) {
    QSettings s;
    s.setValue(QStringLiteral("security/pin"), v);
    s.sync();
}

QStringList AppSettings::trustedDevices() const {
    QSettings s;
    return s.value(QStringLiteral("security/trustedDevices"), QStringList()).toStringList();
}

void AppSettings::addTrustedDevice(const QString& deviceId) {
    QSettings s;
    QStringList list = s.value(QStringLiteral("security/trustedDevices"), QStringList()).toStringList();
    if (!list.contains(deviceId)) {
        list.append(deviceId);
        s.setValue(QStringLiteral("security/trustedDevices"), list);
        s.sync();
    }
}

void AppSettings::removeTrustedDevice(const QString& deviceId) {
    QSettings s;
    QStringList list = s.value(QStringLiteral("security/trustedDevices"), QStringList()).toStringList();
    if (list.removeAll(deviceId) > 0) {
        s.setValue(QStringLiteral("security/trustedDevices"), list);
        s.sync();
    }
}

void AppSettings::clearTrustedDevices() {
    QSettings s;
    s.setValue(QStringLiteral("security/trustedDevices"), QStringList());
    s.sync();
}

} // namespace airshow
