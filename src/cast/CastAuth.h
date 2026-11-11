#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>
#include <optional>

// Cast device-authentication credentials, supplied at runtime.
//
// A Cast sender (Chrome, Android) only talks to receivers that answer its
// AuthChallenge with a certificate chain rooted at Google's Cast root CA and a
// signature over the receiver's TLS peer certificate. The receiver cannot sign
// anything itself, so the credentials come as data: a device certificate, its
// intermediates, the RSA key the TLS peer certificate is built from, the fixed
// subject/serial that peer certificate must carry, and a table of signatures,
// each valid for one fixed time window. This module ships none of that data.
//
// The peer certificate for window i has notBefore = windowStart(i) and
// notAfter = windowStart(i + 1); signatures[i] covers exactly that DER.
//
// JSON format (format 1); binary fields are base64:
//   {
//     "format": 1,
//     "deviceCertificate": "<DER>",
//     "intermediateCertificates": ["<DER>", ...],
//     "peerKey": "<DER private key>",
//     "peerCertificate": { "serial": <int>, "commonName": "<string>" },
//     "signatureStart": <unix seconds>,
//     "signatureWindowSeconds": <seconds>,
//     "signatureSize": <bytes per signature>,
//     "signatures": "<concatenated signatures>"
//   }

namespace airshow::cast {

struct CastAuthCredentials {
    QByteArray        deviceCertificate;          // DER
    QList<QByteArray> intermediateCertificates;   // DER, leaf-most first
    QByteArray        peerKey;                    // DER private key for the TLS peer cert
    uint64_t          peerCertSerial = 0;
    QString           peerCertCommonName;
    uint64_t          signatureStart = 0;         // unix seconds, start of window 0
    uint64_t          signatureWindowSeconds = 0;
    int               signatureSize = 0;
    QByteArray        signatures;                 // signatureCount() * signatureSize bytes

    // Parse the JSON format above. Returns nullopt (and sets *error) when the
    // document is malformed or internally inconsistent.
    static std::optional<CastAuthCredentials> fromJson(const QByteArray& json,
                                                       QString* error = nullptr);

    size_t   signatureCount() const;
    // First second after the last window.
    uint64_t endEpoch() const;
    // Window index for a timestamp, or -1 if it is outside the table.
    int64_t  windowIndex(uint64_t unixTime) const;
    uint64_t windowStart(int64_t index) const;
    bool     isAvailableAt(uint64_t unixTime) const { return windowIndex(unixTime) >= 0; }
    // Signature for the window containing unixTime; empty if there is none.
    QByteArray signatureForTime(uint64_t unixTime) const;
};

} // namespace airshow::cast
