#include "protocol/CastCredentials.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

namespace airshow {

namespace {

std::optional<cast::CastAuthCredentials> loadFrom(const QString& path) {
    QFile file(path);
    if (!file.exists()) return std::nullopt;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cast auth: cannot read %s", qPrintable(path));
        return std::nullopt;
    }
    QString error;
    auto creds = cast::CastAuthCredentials::fromJson(file.readAll(), &error);
    if (!creds)
        qWarning("Cast auth: %s is invalid: %s", qPrintable(path), qPrintable(error));
    return creds;
}

std::optional<cast::CastAuthCredentials> load() {
    QStringList candidates;
    const QString env = qEnvironmentVariable("AIRSHOW_CAST_AUTH");
    if (!env.isEmpty()) candidates << env;
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) candidates << QDir(appData).filePath(QStringLiteral("cast-auth.json"));
    candidates << QStringLiteral(":/cast/cast-auth.json");

    for (const QString& path : candidates) {
        if (auto creds = loadFrom(path)) {
            qDebug("Cast auth: loaded credentials from %s", qPrintable(path));
            return creds;
        }
    }
    qWarning("Cast auth: no usable cast-auth.json found — Google Cast disabled");
    return std::nullopt;
}

} // namespace

const std::optional<cast::CastAuthCredentials>& castCredentials() {
    static const std::optional<cast::CastAuthCredentials> creds = load();
    return creds;
}

} // namespace airshow
