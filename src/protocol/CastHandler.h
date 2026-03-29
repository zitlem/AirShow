#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <memory>
#include <string>
#include <utility>

// Forward declarations — avoid pulling in Qt network headers in the interface
class QSslServer;

namespace myairshow {

class ConnectionBridge;
class CastSession;

// Google Cast CASTV2 protocol handler (D-12, D-13).
//
// Listens on TLS port 8009 for CASTV2 connections from Chrome and Android.
// Uses QSslServer/QSslSocket (Qt 6.4+) for the TLS layer with a runtime-generated
// self-signed certificate. CASTV2 protocol is implemented entirely in CastSession.
//
// Thread model: all I/O runs on the Qt event loop thread — no manual threading.
// New connections replace any existing session (D-14 single-session model).
class CastHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    explicit CastHandler(ConnectionBridge* connectionBridge, QObject* parent = nullptr);
    ~CastHandler() override;

    // ProtocolHandler interface
    bool        start() override;
    void        stop() override;
    std::string name() const override { return "cast"; }
    bool        isRunning() const override { return m_running; }
    void        setMediaPipeline(MediaPipeline* pipeline) override;

private:
    // Generate a runtime self-signed RSA-2048 TLS certificate valid for 48 hours.
    // Uses OpenSSL 3.x EVP API (not deprecated EVP_PKEY_CTX_new_from_name).
    // Returns {certificate, privateKey} as Qt SSL types on success,
    // or {QSslCertificate(), QSslKey()} on failure.
    std::pair<QSslCertificate, QSslKey> generateSelfSignedCert();

    // Called when TLS handshake completes and a new socket is available.
    void onPendingConnection();

    // Clean up when a session emits finished().
    void onSessionFinished();

    QSslServer*                  m_server    = nullptr;
    std::unique_ptr<CastSession> m_session;
    ConnectionBridge*            m_connectionBridge = nullptr;
    MediaPipeline*               m_pipeline         = nullptr;
    bool                         m_running          = false;
};

} // namespace myairshow
