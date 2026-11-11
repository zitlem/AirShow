// Copyright 2026 zitlem
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "cast/CastAuth.h"

#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class QSslServer;

namespace airshow::cast {

class CastReceiverHost;
class CastSession;

// Google Cast (CASTV2) receiver.
//
// Listens on a TLS port (8009 by default) for CASTV2 connections from Chrome and
// Android, answers device authentication with the supplied credentials, and
// hands media to the host. The CASTV2 protocol itself lives in CastSession.
//
// Thread model: all I/O runs on the Qt event loop thread — no manual threading.
//
// Session model:
// - Multiple inactive sessions (auth probes) may coexist so that simultaneous
//   probes from different devices don't starve each other.
// - Once one session goes active (sender sent CONNECT to its transportId), new
//   connections are rejected until it ends.
class CastReceiver : public QObject {
    Q_OBJECT
public:
    CastReceiver(CastReceiverHost* host, CastAuthCredentials credentials,
                 QObject* parent = nullptr);
    ~CastReceiver() override;

    // Returns false if the credentials don't cover the current time, the peer
    // certificate can't be built, or the port can't be bound.
    bool start(uint16_t port = 8009);
    void stop();
    bool isRunning() const { return m_running; }

    const CastAuthCredentials& credentials() const { return m_credentials; }

    // Build the TLS peer certificate for one signature window: credentials'
    // key/serial/CN, notBefore/notAfter = the window's bounds.
    // Returns null Qt types on failure.
    static std::pair<QSslCertificate, QSslKey> makePeerCertificate(
        const CastAuthCredentials& credentials, int64_t windowIndex);

private:
    // Ensure the server's TLS certificate matches the current auth window,
    // regenerating it when the window has rolled over. Called before every
    // TLS handshake. Returns false if no valid window exists or generation failed.
    bool refreshCertificate();

    void onPendingConnection();
    void onSessionFinished();
    CastSession* activeSession() const;

    CastReceiverHost*                         m_host = nullptr;
    CastAuthCredentials                       m_credentials;
    QSslServer*                               m_server = nullptr;
    std::vector<std::unique_ptr<CastSession>> m_sessions;
    bool                                      m_running = false;
    // Auth window the current server certificate was built for (-1 = none yet).
    int64_t                                   m_certWindowIndex = -1;
};

} // namespace airshow::cast
