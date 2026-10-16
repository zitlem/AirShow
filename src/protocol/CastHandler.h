#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Forward declarations — avoid pulling in Qt network headers in the interface
class QSslServer;
class QSslSocket;

namespace airshow {

class ConnectionBridge;
class CastSession;
class SecurityManager;

// Google Cast CASTV2 protocol handler (D-12, D-13).
//
// Listens on TLS port 8009 for CASTV2 connections from Chrome and Android.
// Uses QSslServer/QSslSocket (Qt 6.4+) for the TLS layer with a runtime-generated
// self-signed certificate. CASTV2 protocol is implemented entirely in CastSession.
//
// Thread model: all I/O runs on the Qt event loop thread — no manual threading.
//
// Session model (revised D-14):
// - Multiple inactive sessions (auth probes) are allowed to coexist so that
//   simultaneous probes from different devices don't starve each other.
// - Once one session goes active (sender sent CONNECT), all other sessions are
//   dropped and new connections are rejected until the active session ends.
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

    // Security integration (Phase 7 Plan 02). Call before start().
    // SecurityManager is optional — if null, all connections are admitted (backward compatible).
    void        setSecurityManager(SecurityManager* sm);

private:
    // Build the TLS peer certificate for one auth signature window: fixed
    // AirReceiver key/serial/CN, notBefore/notAfter = the window's bounds.
    // Returns {certificate, privateKey} on success, or null Qt types on failure.
    std::pair<QSslCertificate, QSslKey> generateSelfSignedCert(int64_t windowIndex);

    // Ensure the server's TLS certificate matches the current auth window,
    // regenerating it when the 2-day window has rolled over. Called before every
    // TLS handshake. Returns false if no valid window exists (table expired) or
    // certificate generation failed.
    bool refreshCertificate();

    // Called when TLS handshake completes and a new socket is available.
    void onPendingConnection();

    // Clean up when a session emits finished().
    void onSessionFinished();

    QSslServer*                               m_server           = nullptr;
    // All live sessions — probes and the active cast (if any).
    std::vector<std::unique_ptr<CastSession>> m_sessions;
    ConnectionBridge*                         m_connectionBridge = nullptr;
    MediaPipeline*                            m_pipeline         = nullptr;
    SecurityManager*                          m_securityManager  = nullptr;
    bool                                      m_running          = false;
    // Auth window the current server certificate was built for (-1 = none yet).
    int64_t                                   m_certWindowIndex  = -1;

    // Returns pointer to the active (casting) session, or nullptr.
    CastSession* activeSession() const;
};

} // namespace airshow
