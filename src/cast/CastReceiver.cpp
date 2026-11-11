// Copyright 2026 zitlem
// SPDX-License-Identifier: Apache-2.0

#include "cast/CastReceiver.h"
#include "cast/CastReceiverHost.h"
#include "cast/CastSession.h"

// Qt Network / TLS
#include <QHostAddress>
#include <QSslConfiguration>
#include <QSslServer>
#include <QSslSocket>

// Qt Core
#include <QDateTime>
#include <QDebug>
#include <QTimeZone>

// OpenSSL 3.x — peer certificate generation
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <functional>

namespace airshow::cast {

namespace {

// QSslServer that runs a hook before each incoming connection starts its TLS
// handshake, so the server certificate can be swapped for the current auth
// window first. (pendingConnectionAvailable fires after the handshake — too late.)
class CastSslServer : public QSslServer {
public:
    using QSslServer::QSslServer;
    std::function<void()> beforeHandshake;

protected:
    void incomingConnection(qintptr handle) override {
        if (beforeHandshake) beforeHandshake();
        QSslServer::incomingConnection(handle);
    }
};

QString utcDate(uint64_t secs) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs), QTimeZone::UTC)
        .toString(Qt::ISODate);
}

uint64_t nowSecs() {
    return static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
}

} // namespace

// ── CastReceiver ──────────────────────────────────────────────────────────────

CastReceiver::CastReceiver(CastReceiverHost* host, CastAuthCredentials credentials,
                           QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_credentials(std::move(credentials))
{}

CastReceiver::~CastReceiver() {
    stop();
}

bool CastReceiver::start(uint16_t port) {
    if (m_running) return true;

    // Device auth only works while the signature table covers the current time.
    const uint64_t now = nowSecs();
    const QString expiry = utcDate(m_credentials.endEpoch());
    if (!m_credentials.isAvailableAt(now)) {
        qWarning("CastReceiver: Cast auth signatures do not cover the current time "
                 "(table ends %s) — Google Cast disabled", qPrintable(expiry));
        return false;
    }
    const uint64_t daysLeft = (m_credentials.endEpoch() - now) / 86400;
    if (daysLeft < 90) {
        qWarning("CastReceiver: Cast auth signatures expire on %s (%llu days left) — "
                 "Google Cast will stop working after that",
                 qPrintable(expiry), static_cast<unsigned long long>(daysLeft));
    } else {
        qDebug("CastReceiver: Cast auth signatures valid until %s", qPrintable(expiry));
    }

    auto* server = new CastSslServer(this);
    server->beforeHandshake = [this]() { refreshCertificate(); };
    m_server = server;

    if (!refreshCertificate()) {
        qCritical("CastReceiver: failed to generate TLS peer certificate");
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QSslServer::pendingConnectionAvailable,
            this, &CastReceiver::onPendingConnection);

    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical("CastReceiver: failed to listen on port %u: %s",
                  port, qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_running = true;
    return true;
}

void CastReceiver::stop() {
    if (!m_running) return;
    m_running = false;

    m_sessions.clear();

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_certWindowIndex = -1;
    qDebug("CastReceiver: stopped");
}

bool CastReceiver::refreshCertificate() {
    const int64_t window = m_credentials.windowIndex(nowSecs());
    if (window < 0) {
        qWarning("CastReceiver: no Cast auth signature for the current time — "
                 "senders will reject this receiver");
        return false;
    }
    if (window == m_certWindowIndex) return true;

    auto [cert, key] = makePeerCertificate(m_credentials, window);
    if (cert.isNull() || key.isNull()) return false;

    // Self-signed peer cert; senders trust it via the device-auth signature,
    // not via TLS chain validation.
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setLocalCertificate(cert);
    sslConfig.setPrivateKey(key);
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);  // any sender may connect
    m_server->setSslConfiguration(sslConfig);

    m_certWindowIndex = window;
    qDebug("CastReceiver: TLS certificate set for auth window %lld (%s – %s)",
           static_cast<long long>(window),
           qPrintable(utcDate(m_credentials.windowStart(window))),
           qPrintable(utcDate(m_credentials.windowStart(window + 1))));
    return true;
}

// ── Sessions ──────────────────────────────────────────────────────────────────

CastSession* CastReceiver::activeSession() const {
    for (const auto& s : m_sessions)
        if (s && s->isActive()) return s.get();
    return nullptr;
}

void CastReceiver::onPendingConnection() {
    auto* socket = qobject_cast<QSslSocket*>(m_server->nextPendingConnection());
    if (!socket) return;

    const QHostAddress peerAddr = socket->peerAddress();
    qDebug("CastReceiver: new connection from %s:%d",
           qPrintable(peerAddr.toString()), socket->peerPort());

    if (m_host && !m_host->acceptPeer(peerAddr)) {
        qDebug("CastReceiver: host rejected connection from %s", qPrintable(peerAddr.toString()));
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    // A cast in progress cannot be interrupted by probes.
    if (activeSession()) {
        qDebug("CastReceiver: dropping probe from %s — active session in progress",
               qPrintable(peerAddr.toString()));
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    auto session = std::make_unique<CastSession>(socket, m_host, &m_credentials, this);
    connect(session.get(), &CastSession::finished,
            this, &CastReceiver::onSessionFinished, Qt::QueuedConnection);
    m_sessions.push_back(std::move(session));
}

void CastReceiver::onSessionFinished() {
    CastSession* finished = qobject_cast<CastSession*>(sender());
    auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                           [finished](const std::unique_ptr<CastSession>& s) {
                               return s.get() == finished;
                           });
    if (it != m_sessions.end()) {
        qDebug("CastReceiver: session finished, cleaning up");
        m_sessions.erase(it);
    }
}

// ── Peer certificate ──────────────────────────────────────────────────────────

std::pair<QSslCertificate, QSslKey> CastReceiver::makePeerCertificate(
    const CastAuthCredentials& creds, int64_t windowIndex) {
    const std::pair<QSslCertificate, QSslKey> failed{};

    // The signatures were computed over certificates built from this exact key,
    // serial, subject and validity window. Any difference changes the DER and
    // every sender rejects the receiver.
    const auto* keyPtr = reinterpret_cast<const unsigned char*>(creds.peerKey.constData());
    EVP_PKEY* pkey = d2i_AutoPrivateKey(nullptr, &keyPtr, static_cast<long>(creds.peerKey.size()));
    if (!pkey) {
        qCritical("CastReceiver: failed to parse peer key");
        return failed;
    }

    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return failed;
    }

    X509_set_version(cert, 2);  // X.509 v3
    ASN1_INTEGER_set_uint64(X509_get_serialNumber(cert), creds.peerCertSerial);

    ASN1_TIME_set(X509_getm_notBefore(cert),
                  static_cast<time_t>(creds.windowStart(windowIndex)));
    ASN1_TIME_set(X509_getm_notAfter(cert),
                  static_cast<time_t>(creds.windowStart(windowIndex + 1)));

    X509_set_pubkey(cert, pkey);

    // Build the name separately: X509_get_subject_name() returns const in OpenSSL 4.
    const QByteArray cn = creds.peerCertCommonName.toUtf8();
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(cn.constData()),
                               -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);  // self-signed
    X509_NAME_free(name);

    // sha1WithRSAEncryption — what the signed peer certificates use.
    if (X509_sign(cert, pkey, EVP_sha1()) <= 0) {
        qCritical("CastReceiver: X509_sign failed");
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return failed;
    }

    BIO* certBio = BIO_new(BIO_s_mem());
    BIO* keyBio  = BIO_new(BIO_s_mem());
    const bool written =
        certBio && keyBio && PEM_write_bio_X509(certBio, cert) &&
        PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    X509_free(cert);
    EVP_PKEY_free(pkey);
    if (!written) {
        qCritical("CastReceiver: failed to export peer certificate");
        BIO_free(certBio);
        BIO_free(keyBio);
        return failed;
    }

    char* data = nullptr;
    long  len  = BIO_get_mem_data(certBio, &data);
    const QByteArray certPem(data, static_cast<qsizetype>(len));
    len = BIO_get_mem_data(keyBio, &data);
    const QByteArray keyPem(data, static_cast<qsizetype>(len));
    BIO_free(certBio);
    BIO_free(keyBio);

    QSslCertificate qCert(certPem, QSsl::Pem);
    QSslKey         qKey(keyPem, QSsl::Rsa, QSsl::Pem);
    if (qCert.isNull() || qKey.isNull()) {
        qCritical("CastReceiver: Qt failed to parse generated certificate/key");
        return failed;
    }
    return {qCert, qKey};
}

} // namespace airshow::cast
