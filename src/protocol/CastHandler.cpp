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
#include <QDebug>
#include <QCryptographicHash>

// OpenSSL 3.x (runtime self-signed cert generation per RESEARCH.md Pattern 3)
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

// Cast auth warning
#include "cast/cast_auth_sigs.h"

namespace myairshow {

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

    // Warn about placeholder signatures at startup (D-03 per RESEARCH.md Pattern 3)
    qWarning("Cast auth: using placeholder signatures — Chrome will reject Cast auth "
             "until real signatures are extracted from AirReceiver APK.");

    // Generate a self-signed TLS certificate for port 8009
    auto [cert, key] = generateSelfSignedCert();
    if (cert.isNull() || key.isNull()) {
        qCritical("CastHandler: failed to generate self-signed TLS certificate");
        return false;
    }

    // Configure TLS — self-signed cert is accepted by Chrome tab casting per D-01
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setLocalCertificate(cert);
    sslConfig.setPrivateKey(key);
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    // Peer cert verification not needed (we accept any sender)
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

    m_server = new QSslServer(this);
    m_server->setSslConfiguration(sslConfig);

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

    // Destroy active session first (D-14)
    m_session.reset();

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    qDebug("CastHandler: stopped");
}

void CastHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void CastHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
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

    // Phase 7 (SEC-01): Require SecurityManager approval.
    // CRITICAL: CastHandler runs on the Qt main thread — MUST use checkConnectionAsync.
    // Using checkConnection (QSemaphore blocking) here would deadlock the event loop (Pitfall 1).
    if (m_securityManager) {
        // Use TLS peer certificate fingerprint as stable Cast device ID if available.
        // Falls back to peer IP string if no peer cert (we use VerifyNone, so cert may be absent).
        const QSslCertificate peerCert = socket->peerCertificate();
        QString deviceId = peerCert.isNull()
            ? peerAddr.toString()
            : peerCert.digest(QCryptographicHash::Sha256).toHex();

        // Store socket in QPointer so we can safely check if it was deleted during approval.
        m_pendingSocket = socket;

        // D-14: clear any existing session immediately (new connection replaces old).
        m_session.reset();

        m_securityManager->checkConnectionAsync(
            QStringLiteral("Cast device"), QStringLiteral("Cast"), deviceId,
            [this, socket](bool approved) {
                // This callback is invoked on the Qt main thread by resolveApproval().
                if (!approved) {
                    qDebug("CastHandler: connection denied by security manager");
                    if (m_pendingSocket == socket) {
                        m_pendingSocket = nullptr;
                    }
                    socket->disconnectFromHost();
                    socket->deleteLater();
                    return;
                }
                // Check that the socket is still valid (QPointer catches deletion).
                if (m_pendingSocket != socket || !socket) {
                    qDebug("CastHandler: socket gone before approval resolved");
                    return;
                }
                m_pendingSocket = nullptr;
                qDebug("CastHandler: connection approved, creating CastSession");
                m_session = std::make_unique<CastSession>(
                    socket, m_connectionBridge, m_pipeline, this);
                connect(m_session.get(), &CastSession::finished,
                        this, &CastHandler::onSessionFinished);
            });
    } else {
        // No SecurityManager — admit all local connections (backward compatible).
        // D-14: new connection replaces active session.
        m_session.reset();
        m_session = std::make_unique<CastSession>(socket, m_connectionBridge, m_pipeline, this);
        connect(m_session.get(), &CastSession::finished,
                this, &CastHandler::onSessionFinished);
    }
}

void CastHandler::onSessionFinished() {
    qDebug("CastHandler: session finished, cleaning up");
    m_session.reset();
}

// ── Self-signed certificate generation ───────────────────────────────────────

std::pair<QSslCertificate, QSslKey> CastHandler::generateSelfSignedCert() {
    EVP_PKEY* pkey = nullptr;
    X509*     cert = nullptr;
    BIO*      certBio = nullptr;
    BIO*      keyBio  = nullptr;

    QSslCertificate qCert;
    QSslKey         qKey;

    // Generate RSA-2048 key using OpenSSL 3.x EVP API
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
    if (!ctx) {
        qCritical("CastHandler: EVP_PKEY_CTX_new_from_name failed");
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        qCritical("CastHandler: EVP_PKEY_keygen_init failed");
        EVP_PKEY_CTX_free(ctx);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        qCritical("CastHandler: EVP_PKEY_CTX_set_rsa_keygen_bits failed");
        EVP_PKEY_CTX_free(ctx);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    if (EVP_PKEY_generate(ctx, &pkey) <= 0) {
        qCritical("CastHandler: EVP_PKEY_generate failed");
        EVP_PKEY_CTX_free(ctx);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }
    EVP_PKEY_CTX_free(ctx);

    // Create X.509 v3 self-signed certificate
    cert = X509_new();
    if (!cert) {
        qCritical("CastHandler: X509_new failed");
        EVP_PKEY_free(pkey);
        return std::make_pair(QSslCertificate{}, QSslKey{});
    }

    X509_set_version(cert, 2);  // version 3 (0-indexed)
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60 * 60 * 48);  // 48 hours validity

    X509_set_pubkey(cert, pkey);

    // Set subject and issuer to CN=MyAirShow (self-signed)
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("MyAirShow"),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name);  // self-signed: issuer == subject

    // Sign with SHA-256
    if (X509_sign(cert, pkey, EVP_sha256()) <= 0) {
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

} // namespace myairshow
