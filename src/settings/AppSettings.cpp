#include "settings/AppSettings.h"
#include <QSettings>
#include <QSysInfo>

namespace myairshow {

AppSettings::AppSettings() = default;

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
    return host.isEmpty() ? QStringLiteral("MyAirShow") : host;
}

} // namespace myairshow
