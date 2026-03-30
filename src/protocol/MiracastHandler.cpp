#include "protocol/MiracastHandler.h"
#include "ui/ConnectionBridge.h"
#include "security/SecurityManager.h"
#include "pipeline/MediaPipeline.h"

// Qt Network
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

// Qt Core
#include <QDebug>
#include <QStringConverter>
#include <QtEndian>

namespace myairshow {

// ── MiracastHandler ──────────────────────────────────────────────────────────

MiracastHandler::MiracastHandler(ConnectionBridge* connectionBridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(connectionBridge)
    , m_state(State::Idle)
{}

MiracastHandler::~MiracastHandler() {
    stop();
}

bool MiracastHandler::start() {
    if (m_running) return true;

    m_miceServer = new QTcpServer(this);

    connect(m_miceServer, &QTcpServer::newConnection,
            this, &MiracastHandler::onMiceConnection);

    if (!m_miceServer->listen(QHostAddress::Any, kMicePort)) {
        qCritical("MiracastHandler: failed to listen on port %u: %s",
                  static_cast<unsigned>(kMicePort),
                  qPrintable(m_miceServer->errorString()));
        delete m_miceServer;
        m_miceServer = nullptr;
        return false;
    }

    m_running = true;
    m_state   = State::WaitingSourceReady;
    qDebug("Miracast: listening on port %u (MS-MICE)", static_cast<unsigned>(kMicePort));
    return true;
}

void MiracastHandler::stop() {
    if (!m_running) return;
    m_running = false;
    m_state   = State::Idle;

    // Tear down RTSP client connection
    if (m_rtspSocket) {
        m_rtspSocket->disconnect(this);
        m_rtspSocket->disconnectFromHost();
        m_rtspSocket->deleteLater();
        m_rtspSocket = nullptr;
    }

    // Close accepted MS-MICE client connection
    if (m_miceClient) {
        m_miceClient->disconnect(this);
        m_miceClient->disconnectFromHost();
        m_miceClient->deleteLater();
        m_miceClient = nullptr;
    }

    // Close the listening server
    if (m_miceServer) {
        m_miceServer->close();
        delete m_miceServer;
        m_miceServer = nullptr;
    }

    // Stop pipeline if streaming
    if (m_pipeline) {
        m_pipeline->stopMiracast();
    }

    qDebug("MiracastHandler: stopped");
}

void MiracastHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void MiracastHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
}

void MiracastHandler::setQmlVideoItem(void* item) {
    m_qmlVideoItem = item;
}

// ── Private slots ─────────────────────────────────────────────────────────────

void MiracastHandler::onMiceConnection() {
    QTcpSocket* socket = m_miceServer->nextPendingConnection();
    if (!socket) return;

    m_sourceAddr = socket->peerAddress();
    qDebug("MiracastHandler: MS-MICE connection from %s:%d",
           qPrintable(m_sourceAddr.toString()),
           socket->peerPort());

    // Single-session model: close any existing connection
    if (m_miceClient) {
        m_miceClient->disconnect(this);
        m_miceClient->disconnectFromHost();
        m_miceClient->deleteLater();
    }

    m_miceClient = socket;
    connect(m_miceClient, &QTcpSocket::readyRead,
            this, &MiracastHandler::onMiceData);

    m_state = State::WaitingSourceReady;
}

void MiracastHandler::onMiceData() {
    if (!m_miceClient) return;
    const QByteArray data = m_miceClient->readAll();
    parseMiceMessage(data);
}

void MiracastHandler::parseMiceMessage(const QByteArray& data) {
    // Delegate to the static parser for testability
    SourceReadyInfo info;
    if (!parseSourceReady(data, info)) {
        qWarning("MiracastHandler: failed to parse MS-MICE message (%lld bytes)",
                 static_cast<long long>(data.size()));
        return;
    }

    m_sourceName = info.friendlyName;
    m_rtspPort   = info.rtspPort > 0 ? info.rtspPort : kRtspPort;
    m_sourceId   = info.sourceId;

    qDebug("MiracastHandler: SOURCE_READY parsed — name='%s' rtspPort=%u sourceId='%s'",
           qPrintable(m_sourceName),
           static_cast<unsigned>(m_rtspPort),
           qPrintable(m_sourceId));

    // Phase 7 (SEC-03): Reject connections from non-RFC1918 addresses (RESEARCH.md Pitfall 7)
    if (m_securityManager && !SecurityManager::isLocalNetwork(m_sourceAddr)) {
        qWarning("MiracastHandler: rejecting non-local source %s",
                 qPrintable(m_sourceAddr.toString()));
        if (m_miceClient) {
            m_miceClient->disconnectFromHost();
        }
        m_state = State::WaitingSourceReady;
        return;
    }

    // Phase 7 (SEC-01): Require SecurityManager approval before connecting.
    // MiracastHandler runs on the Qt main thread (same as CastHandler) — MUST use async.
    if (m_securityManager) {
        m_securityManager->checkConnectionAsync(
            m_sourceName, QStringLiteral("miracast"), m_sourceAddr.toString(),
            [this](bool approved) {
                if (!approved) {
                    qDebug("MiracastHandler: connection denied by security manager");
                    if (m_miceClient) {
                        m_miceClient->disconnectFromHost();
                    }
                    m_state = State::WaitingSourceReady;
                    return;
                }
                connectToSource();
            });
    } else {
        // No security manager — connect directly (backward compatible)
        connectToSource();
    }
}

// static
bool MiracastHandler::parseSourceReady(const QByteArray& data, SourceReadyInfo& out) {
    // MS-MICE SOURCE_READY binary format (RESEARCH.md Pattern 1, Pitfall 2):
    //   [2 bytes big-endian: total message size]
    //   [1 byte: version, expected 0x01]
    //   [1 byte: command, 0x01 = SOURCE_READY]
    //   followed by TLV records:
    //     [2 bytes big-endian: TLV type]
    //     [2 bytes big-endian: TLV length]
    //     [length bytes: TLV value]
    //   Known types:
    //     0x0001: FriendlyName (UTF-16LE)
    //     0x0002: RTSPPort (big-endian uint16)
    //     0x0003: SourceID (ASCII/UTF-8)

    constexpr int kMinSize = 4;  // 2-byte size + version + command
    if (data.size() < kMinSize) {
        return false;
    }

    // Parse 4-byte header
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(data.constData());
    const uint16_t msgSize = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    const uint8_t  version = buf[2];
    const uint8_t  command = buf[3];

    if (version != 0x01) {
        qWarning("MiracastHandler: unexpected SOURCE_READY version 0x%02X", version);
        // Continue parsing anyway — be lenient
    }
    if (command != 0x01) {
        qWarning("MiracastHandler: unexpected MS-MICE command 0x%02X (expected 0x01 SOURCE_READY)",
                 command);
        return false;
    }

    // Clamp parse range to actual buffer size (guard against truncated messages)
    const int parseLen = qMin(static_cast<int>(msgSize), data.size());

    int pos = 4;
    while (pos + 4 <= parseLen) {
        const uint16_t tlvType = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        const uint16_t tlvLen  = (static_cast<uint16_t>(buf[pos + 2]) << 8) | buf[pos + 3];
        pos += 4;

        if (pos + tlvLen > parseLen) {
            qWarning("MiracastHandler: TLV 0x%04X length %u exceeds message bounds",
                     tlvType, tlvLen);
            break;
        }

        const QByteArray tlvValue(reinterpret_cast<const char*>(buf + pos), tlvLen);
        pos += tlvLen;

        switch (tlvType) {
        case 0x0001: {
            // FriendlyName: UTF-16LE encoded string
            auto decoder = QStringDecoder(QStringDecoder::Utf16LE);
            out.friendlyName = decoder.decode(tlvValue);
            break;
        }
        case 0x0002: {
            // RTSPPort: big-endian uint16
            if (tlvLen >= 2) {
                out.rtspPort = (static_cast<uint16_t>(
                    static_cast<uint8_t>(tlvValue[0])) << 8) |
                    static_cast<uint8_t>(tlvValue[1]);
            }
            break;
        }
        case 0x0003: {
            // SourceID: ASCII/UTF-8 string
            out.sourceId = QString::fromUtf8(tlvValue);
            break;
        }
        default:
            qDebug("MiracastHandler: ignoring unknown TLV type 0x%04X len=%u",
                   tlvType, tlvLen);
            break;
        }
    }

    return true;
}

// ── RTSP connection ───────────────────────────────────────────────────────────

void MiracastHandler::connectToSource() {
    // Create RTSP client socket and connect to source:7236
    if (m_rtspSocket) {
        m_rtspSocket->disconnect(this);
        m_rtspSocket->disconnectFromHost();
        m_rtspSocket->deleteLater();
        m_rtspSocket = nullptr;
    }

    m_rtspBuffer.clear();
    m_cseq = 1;

    m_rtspSocket = new QTcpSocket(this);
    connect(m_rtspSocket, &QTcpSocket::connected,
            this, &MiracastHandler::onRtspConnected);
    connect(m_rtspSocket, &QTcpSocket::readyRead,
            this, &MiracastHandler::onRtspData);
    connect(m_rtspSocket, &QTcpSocket::disconnected,
            this, &MiracastHandler::onRtspDisconnected);

    m_state = State::ConnectingToSource;
    qDebug("Miracast: connecting to RTSP server at %s:%u",
           qPrintable(m_sourceAddr.toString()),
           static_cast<unsigned>(m_rtspPort));
    m_rtspSocket->connectToHost(m_sourceAddr, m_rtspPort);
}

void MiracastHandler::onRtspConnected() {
    // Source sends M1 OPTIONS first — wait for data
    m_state = State::NegotiatingM1;
    qDebug("Miracast: RTSP connected to source — waiting for M1 OPTIONS");
}

void MiracastHandler::onRtspData() {
    if (!m_rtspSocket) return;
    m_rtspBuffer.append(m_rtspSocket->readAll());

    // Process all complete messages in the buffer
    RtspMessage msg;
    while (parseNextRtspMessage(msg)) {
        qDebug("Miracast: RTSP received [%s] state=%d cseq=%d status=%d",
               msg.isRequest ? qPrintable(msg.method) : "response",
               static_cast<int>(m_state),
               msg.cseq,
               msg.statusCode);

        switch (m_state) {
        case State::NegotiatingM1:
            // M1: OPTIONS request from source with "Require: org.wfa.wfd1.0"
            if (msg.isRequest && msg.method == QStringLiteral("OPTIONS")) {
                // Send 200 OK with WFD capabilities in Public header
                const QString publicHeader =
                    QStringLiteral("Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, "
                                   "SETUP, PLAY, PAUSE, TEARDOWN\r\n");
                // Build M1 200 OK manually (Public header doesn't fit buildRtspResponse)
                const QString m1Resp =
                    QStringLiteral("RTSP/1.0 200 OK\r\n"
                                   "CSeq: %1\r\n"
                                   "Server: MyAirShow/1.0\r\n"
                                   "%2\r\n")
                    .arg(msg.cseq)
                    .arg(publicHeader);
                m_rtspSocket->write(m1Resp.toUtf8());
                qDebug("Miracast: sent M1 200 OK");

                // Immediately send M2 OPTIONS (RESEARCH.md Pitfall 3 — M2 is NOT optional)
                sendRtspRequest(QStringLiteral("OPTIONS"),
                                QStringLiteral("*"),
                                QStringLiteral("Require: org.wfa.wfd1.0\r\n"));
                m_state = State::NegotiatingM2;
                qDebug("Miracast: sent M2 OPTIONS");
            }
            break;

        case State::NegotiatingM2:
            // M2: 200 OK response to our OPTIONS
            if (!msg.isRequest && msg.statusCode == 200) {
                m_state = State::NegotiatingM3;
                qDebug("Miracast: M2 acknowledged — waiting for M3 GET_PARAMETER");
            }
            break;

        case State::NegotiatingM3:
            // M3: GET_PARAMETER from source requesting our capabilities
            if (msg.isRequest && msg.method == QStringLiteral("GET_PARAMETER")) {
                const QString capResp = QString::fromUtf8(kWfdCapabilityResponse);
                const QString m3Resp = buildRtspResponse(msg.cseq, 200, capResp);
                m_rtspSocket->write(m3Resp.toUtf8());
                m_state = State::NegotiatingM4;
                qDebug("Miracast: sent M3 capability response");
            }
            break;

        case State::NegotiatingM4:
            // M4: SET_PARAMETER from source with selected codec + RTP ports
            if (msg.isRequest && msg.method == QStringLiteral("SET_PARAMETER")) {
                qDebug("Miracast: M4 SET_PARAMETER received — selected params:\n%s",
                       msg.body.constData());
                const QString m4Resp = buildRtspResponse(msg.cseq, 200);
                m_rtspSocket->write(m4Resp.toUtf8());
                m_state = State::NegotiatingM5;
                qDebug("Miracast: sent M4 200 OK");
            }
            break;

        case State::NegotiatingM5:
            // M5: SET_PARAMETER with wfd_trigger_method: SETUP
            if (msg.isRequest && msg.method == QStringLiteral("SET_PARAMETER")) {
                const QString m5Resp = buildRtspResponse(msg.cseq, 200);
                m_rtspSocket->write(m5Resp.toUtf8());
                qDebug("Miracast: sent M5 200 OK — sending M6 SETUP");

                // Send M6 SETUP
                const QString rtspUri = QStringLiteral("rtsp://%1:%2/wfd1.0/streamid=0")
                    .arg(m_sourceAddr.toString())
                    .arg(m_rtspPort);
                const QString transportHeader =
                    QStringLiteral("Transport: RTP/AVP/UDP;unicast;client_port=%1\r\n")
                    .arg(m_udpPort);
                sendRtspRequest(QStringLiteral("SETUP"), rtspUri, transportHeader);
                m_state = State::SendingSetup;
                qDebug("Miracast: sent M6 SETUP (client_port=%d)", m_udpPort);
            }
            break;

        case State::SendingSetup:
            // M6 response: 200 OK with Transport header containing server_port
            if (!msg.isRequest && msg.statusCode == 200) {
                qDebug("Miracast: M6 SETUP accepted — sending M7 PLAY");

                // Initialize the GStreamer MPEG-TS/RTP receive pipeline
                if (m_pipeline) {
                    if (!m_pipeline->initMiracastPipeline(m_qmlVideoItem, m_udpPort)) {
                        qWarning("Miracast: failed to initialize media pipeline");
                    }
                }

                // Send M7 PLAY
                const QString rtspUri = QStringLiteral("rtsp://%1:%2/wfd1.0/streamid=0")
                    .arg(m_sourceAddr.toString())
                    .arg(m_rtspPort);
                sendRtspRequest(QStringLiteral("PLAY"), rtspUri);
                m_state = State::SendingPlay;
                qDebug("Miracast: sent M7 PLAY");
            }
            break;

        case State::SendingPlay:
            // M7 response: 200 OK — stream begins
            if (!msg.isRequest && msg.statusCode == 200) {
                qDebug("Miracast: streaming from '%s'", qPrintable(m_sourceName));

                // Start the pipeline
                if (m_pipeline) {
                    m_pipeline->play();
                }

                // Update HUD (D-16)
                if (m_connectionBridge) {
                    m_connectionBridge->setConnected(true, m_sourceName,
                                                     QStringLiteral("miracast"));
                }

                m_state = State::Streaming;
                qDebug("Miracast: streaming from %s", qPrintable(m_sourceName));
            }
            break;

        case State::Streaming:
            // Handle keepalive GET_PARAMETER from source
            if (msg.isRequest && msg.method == QStringLiteral("GET_PARAMETER")) {
                const QString keepaliveResp = buildRtspResponse(msg.cseq, 200);
                m_rtspSocket->write(keepaliveResp.toUtf8());
                qDebug("Miracast: keepalive GET_PARAMETER acknowledged");
            }
            // Handle TEARDOWN from source
            else if (msg.isRequest && msg.method == QStringLiteral("TEARDOWN")) {
                const QString tearResp = buildRtspResponse(msg.cseq, 200);
                m_rtspSocket->write(tearResp.toUtf8());
                qDebug("Miracast: TEARDOWN received from source");
                teardown();
            }
            break;

        default:
            qDebug("Miracast: ignoring RTSP message in state %d", static_cast<int>(m_state));
            break;
        }
    }
}

void MiracastHandler::onRtspDisconnected() {
    qDebug("MiracastHandler: RTSP socket disconnected (state=%d)", static_cast<int>(m_state));
    if (m_state == State::Streaming) {
        teardown();
    } else {
        // Clean up and reset to waiting for next connection
        if (m_rtspSocket) {
            m_rtspSocket->deleteLater();
            m_rtspSocket = nullptr;
        }
        m_rtspBuffer.clear();
        if (m_running) {
            m_state = State::WaitingSourceReady;
        } else {
            m_state = State::Idle;
        }
    }
}

// ── Helper methods ─────────────────────────────────────────────────────────────

void MiracastHandler::teardown() {
    m_state = State::TearingDown;
    qDebug("Miracast: session ended");

    if (m_pipeline) {
        m_pipeline->stopMiracast();
    }

    if (m_connectionBridge) {
        m_connectionBridge->setConnected(false);
    }

    if (m_rtspSocket) {
        m_rtspSocket->disconnect(this);
        m_rtspSocket->disconnectFromHost();
        m_rtspSocket->deleteLater();
        m_rtspSocket = nullptr;
    }

    if (m_miceClient) {
        m_miceClient->disconnect(this);
        m_miceClient->disconnectFromHost();
        m_miceClient->deleteLater();
        m_miceClient = nullptr;
    }

    m_rtspBuffer.clear();
    m_sourceName.clear();
    m_sourceId.clear();
    m_cseq = 1;

    // Return to waiting for next connection (server still listening)
    if (m_running) {
        m_state = State::WaitingSourceReady;
    } else {
        m_state = State::Idle;
    }
}

void MiracastHandler::sendRtspRequest(const QString& method, const QString& uri,
                                       const QString& extraHeaders, const QString& body) {
    if (!m_rtspSocket) {
        qWarning("MiracastHandler::sendRtspRequest called with no RTSP socket");
        return;
    }

    QString request = QStringLiteral("%1 %2 RTSP/1.0\r\nCSeq: %3\r\n")
        .arg(method)
        .arg(uri)
        .arg(m_cseq++);

    if (!extraHeaders.isEmpty()) {
        request += extraHeaders;
    }

    if (!body.isEmpty()) {
        const QByteArray bodyBytes = body.toUtf8();
        request += QStringLiteral("Content-Type: text/parameters\r\n");
        request += QStringLiteral("Content-Length: %1\r\n").arg(bodyBytes.size());
        request += QStringLiteral("\r\n");
        request += body;
    } else {
        request += QStringLiteral("\r\n");
    }

    m_rtspSocket->write(request.toUtf8());
}

// static
QString MiracastHandler::buildRtspResponse(int cseq, int statusCode, const QString& body) {
    // Build a RTSP/1.0 response with CSeq header.
    // Status text map for common codes used in WFD exchange.
    QString statusText;
    switch (statusCode) {
    case 200: statusText = "OK";                    break;
    case 400: statusText = "Bad Request";           break;
    case 404: statusText = "Not Found";             break;
    case 454: statusText = "Session Not Found";     break;
    case 500: statusText = "Internal Server Error"; break;
    default:  statusText = "Unknown";               break;
    }

    QString response = QStringLiteral("RTSP/1.0 %1 %2\r\nCSeq: %3\r\nServer: MyAirShow/1.0\r\n")
                           .arg(statusCode)
                           .arg(statusText)
                           .arg(cseq);

    if (!body.isEmpty()) {
        const QByteArray bodyBytes = body.toUtf8();
        response += QStringLiteral("Content-Type: text/parameters\r\n");
        response += QStringLiteral("Content-Length: %1\r\n").arg(bodyBytes.size());
        response += QStringLiteral("\r\n");
        response += body;
    } else {
        response += QStringLiteral("\r\n");
    }

    return response;
}

bool MiracastHandler::parseNextRtspMessage(RtspMessage& msg) {
    // RTSP messages are delimited by \r\n\r\n (end of headers),
    // with optional body of Content-Length bytes after that.
    // This function parses one complete message from m_rtspBuffer.

    // Find end of headers
    const int headerEnd = m_rtspBuffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;  // Incomplete headers
    }

    const QByteArray headerBlock = m_rtspBuffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');

    if (lines.isEmpty()) return false;

    // Parse first line: "METHOD uri RTSP/1.0" or "RTSP/1.0 STATUS text"
    QString firstLine = QString::fromUtf8(lines[0]).trimmed();
    msg = RtspMessage{};

    if (firstLine.startsWith(QStringLiteral("RTSP/1.0"))) {
        // Response line
        msg.isRequest = false;
        const QStringList parts = firstLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            msg.statusCode = parts[1].toInt();
        }
    } else {
        // Request line: METHOD uri RTSP/1.0
        msg.isRequest = true;
        const QStringList parts = firstLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 1) msg.method = parts[0];
        if (parts.size() >= 2) msg.uri = parts[1];
    }

    // Parse header lines
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines[i]).trimmed();
        if (line.isEmpty()) continue;

        const int colonIdx = line.indexOf(QLatin1Char(':'));
        if (colonIdx < 0) continue;

        const QString key   = line.left(colonIdx).trimmed().toLower();
        const QString value = line.mid(colonIdx + 1).trimmed();

        if (key == QStringLiteral("cseq")) {
            msg.cseq = value.toInt();
        } else if (key == QStringLiteral("content-length")) {
            msg.contentLength = value.toInt();
        } else if (key == QStringLiteral("content-type")) {
            msg.contentType = value;
        }
    }

    // Check if body is complete
    const int messageEnd = headerEnd + 4 + msg.contentLength;
    if (m_rtspBuffer.size() < messageEnd) {
        return false;  // Body incomplete
    }

    if (msg.contentLength > 0) {
        msg.body = m_rtspBuffer.mid(headerEnd + 4, msg.contentLength);
    }

    // Remove consumed bytes from buffer
    m_rtspBuffer.remove(0, messageEnd);
    return true;
}

} // namespace myairshow
