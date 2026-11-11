// Copyright 2026 zitlem
// SPDX-License-Identifier: Apache-2.0

#include "cast/CastAuth.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace airshow::cast {

namespace {

bool decodeBase64(const QJsonValue& v, QByteArray* out) {
    if (!v.isString()) return false;
    auto result = QByteArray::fromBase64Encoding(v.toString().toLatin1(),
                                                 QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.isEmpty()) return false;
    *out = std::move(result.decoded);
    return true;
}

// QJsonValue holds numbers as double; reject anything that isn't a
// non-negative integer that survives the round trip.
bool readUInt(const QJsonValue& v, uint64_t* out) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    if (d < 0 || d > 9007199254740992.0 || d != static_cast<double>(static_cast<uint64_t>(d)))
        return false;
    *out = static_cast<uint64_t>(d);
    return true;
}

} // namespace

std::optional<CastAuthCredentials> CastAuthCredentials::fromJson(const QByteArray& json,
                                                                 QString* error) {
    auto fail = [error](const QString& why) -> std::optional<CastAuthCredentials> {
        if (error) *error = why;
        return std::nullopt;
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) return fail(QStringLiteral("not a JSON object: ") + parseError.errorString());
    const QJsonObject o = doc.object();

    if (o.value(QStringLiteral("format")).toInt(-1) != 1)
        return fail(QStringLiteral("unsupported format (expected 1)"));

    CastAuthCredentials c;
    if (!decodeBase64(o.value(QStringLiteral("deviceCertificate")), &c.deviceCertificate))
        return fail(QStringLiteral("deviceCertificate missing or not base64"));
    for (const QJsonValue& v : o.value(QStringLiteral("intermediateCertificates")).toArray()) {
        QByteArray der;
        if (!decodeBase64(v, &der))
            return fail(QStringLiteral("intermediateCertificates entry not base64"));
        c.intermediateCertificates.append(der);
    }
    if (!decodeBase64(o.value(QStringLiteral("peerKey")), &c.peerKey))
        return fail(QStringLiteral("peerKey missing or not base64"));

    const QJsonObject peer = o.value(QStringLiteral("peerCertificate")).toObject();
    if (!readUInt(peer.value(QStringLiteral("serial")), &c.peerCertSerial))
        return fail(QStringLiteral("peerCertificate.serial missing"));
    c.peerCertCommonName = peer.value(QStringLiteral("commonName")).toString();
    if (c.peerCertCommonName.isEmpty())
        return fail(QStringLiteral("peerCertificate.commonName missing"));

    uint64_t size = 0;
    if (!readUInt(o.value(QStringLiteral("signatureStart")), &c.signatureStart) ||
        !readUInt(o.value(QStringLiteral("signatureWindowSeconds")), &c.signatureWindowSeconds) ||
        c.signatureWindowSeconds == 0 ||
        !readUInt(o.value(QStringLiteral("signatureSize")), &size) || size == 0 || size > 4096)
        return fail(QStringLiteral("signatureStart/signatureWindowSeconds/signatureSize invalid"));
    c.signatureSize = static_cast<int>(size);

    if (!decodeBase64(o.value(QStringLiteral("signatures")), &c.signatures) ||
        c.signatures.size() % c.signatureSize != 0)
        return fail(QStringLiteral("signatures missing or not a multiple of signatureSize"));

    return c;
}

size_t CastAuthCredentials::signatureCount() const {
    return signatureSize > 0 ? static_cast<size_t>(signatures.size()) / signatureSize : 0;
}

uint64_t CastAuthCredentials::endEpoch() const {
    return signatureStart + signatureCount() * signatureWindowSeconds;
}

int64_t CastAuthCredentials::windowIndex(uint64_t unixTime) const {
    if (signatureWindowSeconds == 0 || unixTime < signatureStart || unixTime >= endEpoch())
        return -1;
    return static_cast<int64_t>((unixTime - signatureStart) / signatureWindowSeconds);
}

uint64_t CastAuthCredentials::windowStart(int64_t index) const {
    return signatureStart + static_cast<uint64_t>(index) * signatureWindowSeconds;
}

QByteArray CastAuthCredentials::signatureForTime(uint64_t unixTime) const {
    const int64_t index = windowIndex(unixTime);
    if (index < 0) return {};
    return signatures.mid(static_cast<qsizetype>(index) * signatureSize, signatureSize);
}

} // namespace airshow::cast
