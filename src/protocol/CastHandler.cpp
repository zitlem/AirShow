#include "protocol/CastHandler.h"
#include "protocol/CastSession.h"
#include "ui/ConnectionBridge.h"
#include "security/SecurityManager.h"

// Qt Network / TLS
#include <QSslServer>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QHostAddress>

// Qt Core
#include <QDateTime>
#include <QDebug>

// OpenSSL 3.x (runtime self-signed cert generation per RESEARCH.md Pattern 3)
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

// Cast auth: fixed peer key + signature window math
#include "cast/cast_auth_sigs.h"

#include <QTimeZone>

#include <algorithm>
#include <functional>

namespace airshow {

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

} // namespace

// ── CastHandler ───────────────────────────────────────────────────────────────

CastHandler::CastHandler(ConnectionBridge* connectionBridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(connectionBridge)
{}

CastHandler::~CastHandler() {
    stop();
}

bool CastHandler::start() {
    if (m_running) return true;

    // Cast device auth only works while the precomputed signature table covers
    // the current time. Past the end there is no valid signature to send.
    const uint64_t nowSecs = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    const QString expiry = utcDate(cast::kCastAuthEndEpoch);
    if (!cast::isCastAuthAvailable(nowSecs)) {
        qWarning("CastHandler: Cast auth signatures expired on %s — Google Cast disabled",
                 qPrintable(expiry));
        return false;
    }
    const uint64_t daysLeft = (cast::kCastAuthEndEpoch - nowSecs) / 86400;
    if (daysLeft < 90) {
        qWarning("CastHandler: Cast auth signatures expire on %s (%llu days left) — "
                 "Google Cast will stop working after that",
                 qPrintable(expiry), static_cast<unsigned long long>(daysLeft));
    } else {
        qDebug("Cast auth: using signatures from shanocast/AirReceiver (valid until %s)",
               qPrintable(expiry));
    }

    auto* server = new CastSslServer(this);
    server->beforeHandshake = [this]() { refreshCertificate(); };
    m_server = server;

    if (!refreshCertificate()) {
        qCritical("CastHandler: failed to generate self-signed TLS certificate");
        delete m_server;
        m_server = nullptr;
        return false;
    }

    // Accept new connections
    connect(m_server, &QSslServer::pendingConnectionAvailable,
            this, &CastHandler::onPendingConnection);

    if (!m_server->listen(QHostAddress::Any, 8009)) {
        qCritical("CastHandler: failed to listen on port 8009: %s",
                  qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_running = true;
    qDebug("CastHandler: listening on port 8009 (TLS)");
    return true;
}

void CastHandler::stop() {
    if (!m_running) return;
    m_running = false;

    m_sessions.clear();

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_certWindowIndex = -1;
    qDebug("CastHandler: stopped");
}

bool CastHandler::refreshCertificate() {
    const uint64_t nowSecs = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    const int64_t window = cast::castAuthWindowIndex(nowSecs);
    if (window < 0) {
        qWarning("CastHandler: no Cast auth signature for the current time — "
                 "senders will reject this receiver");
        return false;
    }
    if (window == m_certWindowIndex) return true;

    auto [cert, key] = generateSelfSignedCert(window);
    if (cert.isNull() || key.isNull()) return false;

    // Self-signed peer cert; Chrome trusts it via the device-auth signature,
    // not via TLS chain validation.
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setLocalCertificate(cert);
    sslConfig.setPrivateKey(key);
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    // Peer cert verification not needed (we accept any sender)
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_server->setSslConfiguration(sslConfig);

    m_certWindowIndex = window;
    qDebug("CastHandler: TLS certificate set for auth window %lld (%s – %s)",
           static_cast<long long>(window),
           qPrintable(utcDate(cast::castAuthWindowStart(window))),
           qPrintable(utcDate(cast::castAuthWindowStart(window + 1))));
    return true;
}

void CastHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void CastHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

CastSession* CastHandler::activeSession() const {
    for (const auto& s : m_sessions)
        if (s && s->isActive()) return s.get();
    return nullptr;
}

// ── Private slots ─────────────────────────────────────────────────────────────

void CastHandler::onPendingConnection() {
    auto* socket = qobject_cast<QSslSocket*>(m_server->nextPendingConnection());
    if (!socket) return;

    const QHostAddress peerAddr = socket->peerAddress();
    qDebug("CastHandler: new connection from %s:%d",
           qPrintable(peerAddr.toString()),
           socket->peerPort());

    // Phase 7 (SEC-03): Reject connections from non-RFC1918 source IPs immediately.
    if (!SecurityManager::isLocalNetwork(peerAddr)) {
        qDebug("CastHandler: rejecting non-local connection from %s",
               qPrintable(peerAddr.toString()));
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    // D-14 (revised): if there is an ACTIVE (casting) session, reject all new
    // connections — a cast is in progress and cannot be interrupted by probes.
    if (activeSession()) {
        qDebug("CastHandler: dropping probe from %s — active session in progress",
               qPrintable(peerAddr.toString()));
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    // No active session: accept this connection as a new probe/auth session.
    // Multiple inactive sessions (auth probes) are allowed to coexist so that
    // simultaneous probes from different devices don't starve each other.
    qDebug("CastHandler: connection approved, creating CastSession");
    auto session = std::make_unique<CastSession>(
        socket, m_connectionBridge, m_pipeline, m_securityManager, this);
    connect(session.get(), &CastSession::finished,
            this, &CastHandler::onSessionFinished, Qt::QueuedConnection);
    m_sessions.push_back(std::move(session));
}

void CastHandler::onSessionFinished() {
    CastSession* finished = qobject_cast<CastSession*>(sender());
    // Remove the finished session from the list.
    auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                           [finished](const std::unique_ptr<CastSession>& s) {
                               return s.get() == finished;
                           });
    if (it != m_sessions.end()) {
        qDebug("CastHandler: session finished, cleaning up");
        m_sessions.erase(it);
    }
}

// ── Self-signed certificate generation ───────────────────────────────────────

std::pair<QSslCertificate, QSslKey> CastHandler::generateSelfSignedCert(int64_t windowIndex) {
    EVP_PKEY* pkey = nullptr;
    X509*     cert = nullptr;
    BIO*      certBio = nullptr;
    BIO*      keyBio  = nullptr;

    QSslCertificate qCert;
    QSslKey         qKey;

    // CRITICAL: Use the fixed peer_key_der from shanocast/AirReceiver.
    // The precomputed signatures in cast_auth_sigs.h were computed against
    // certificates generated with this specific RSA key. If we use a different
    // key, Chrome will reject the signature verification.
    const unsigned char* keyPtr = cast::peer_key_der;
    pkey = d2i_AutoPrivateKey(nullptr, &keyPtr, cast::peer_key_der_len);
    if (!pkey) {
        qCritical("CastHandler: failed to parse peer_key_der");
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    // Create X.509 v3 self-signed certificate (peer certificate)
    // Valid for 48 hours (matches shanocast pattern), signed with SHA-1
    // (shanocast uses SHA-1 for the peer cert signature)
    cert = X509_new();
    if (!cert) {
        qCritical("CastHandler: X509_new failed");
        EVP_PKEY_free(pkey);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    X509_set_version(cert, 2);  // version 3 (0-indexed)
    // Serial number 0x51c9ac9 — matches AirReceiver APK (confirmed from jnitrace capture).
    // The precomputed signatures in cast_auth_sigs.h were computed against certs with this
    // exact serial. Using 0x51c9ac6 or any other value produces a different DER → signature mismatch.
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0x51c9ac9);

    // Timestamps MUST be aligned to the same 2-day window boundaries used when computing
    // the precomputed signatures. AirReceiver aligns cert validity to the window: notBefore
    // = window_start, notAfter = window_start + 172800s. Using now±24h produces a different
    // DER each startup, so the precomputed signature never verifies.
    const time_t windowStart = static_cast<time_t>(cast::castAuthWindowStart(windowIndex));
    const time_t windowEnd   = static_cast<time_t>(cast::castAuthWindowStart(windowIndex + 1));
    ASN1_TIME_set(X509_getm_notBefore(cert), windowStart);
    ASN1_TIME_set(X509_getm_notAfter(cert),  windowEnd);

    X509_set_pubkey(cert, pkey);

    // CN "4aa9ca2e-c340-11ea-8000-18ba395587df" — the UUID embedded in AirReceiver APK.
    // Confirmed via jnitrace on the original app. Any other CN changes the cert DER.
    // Build the name separately: X509_get_subject_name() returns const in OpenSSL 4.
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(
                                   "4aa9ca2e-c340-11ea-8000-18ba395587df"),
                               -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);  // self-signed: issuer == subject
    X509_NAME_free(name);

    // Sign with SHA-1 (AirReceiver uses sha1WithRSAEncryption for the peer cert)
    if (X509_sign(cert, pkey, EVP_sha1()) <= 0) {
        qCritical("CastHandler: X509_sign failed");
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    // Export certificate to PEM via BIO
    certBio = BIO_new(BIO_s_mem());
    if (!certBio || !PEM_write_bio_X509(certBio, cert)) {
        qCritical("CastHandler: failed to write cert PEM");
        if (certBio) BIO_free(certBio);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    // Export private key to PEM via BIO
    keyBio = BIO_new(BIO_s_mem());
    if (!keyBio || !PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        qCritical("CastHandler: failed to write key PEM");
        BIO_free(certBio);
        if (keyBio) BIO_free(keyBio);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    // Read PEM bytes into QByteArrays
    char* certData = nullptr;
    long  certLen  = BIO_get_mem_data(certBio, &certData);
    QByteArray certPem(certData, static_cast<int>(certLen));

    char* keyData = nullptr;
    long  keyLen  = BIO_get_mem_data(keyBio, &keyData);
    QByteArray keyPem(keyData, static_cast<int>(keyLen));

    BIO_free(certBio);
    BIO_free(keyBio);
    X509_free(cert);
    EVP_PKEY_free(pkey);

    // Construct Qt SSL types from PEM
    qCert = QSslCertificate(certPem, QSsl::Pem);
    qKey  = QSslKey(keyPem, QSsl::Rsa, QSsl::Pem);

    if (qCert.isNull()) {
        qCritical("CastHandler: QSslCertificate failed to parse generated PEM");
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }
    if (qKey.isNull()) {
        qCritical("CastHandler: QSslKey failed to parse generated PEM");
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    qDebug("CastHandler: self-signed TLS certificate generated successfully");
    return {qCert, qKey};
}

} // namespace airshow
