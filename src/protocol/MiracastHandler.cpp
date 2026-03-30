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

    // Plan 02 will initiate the RTSP connection here.
    // For now, log that we received SOURCE_READY and stop at this point.
    qDebug("MiracastHandler: RTSP connection (Plan 02) not yet implemented — "
           "source IP=%s rtspPort=%u",
           qPrintable(m_sourceAddr.toString()),
           static_cast<unsigned>(m_rtspPort));
    m_state = State::ConnectingToSource;
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

// ── RTSP stubs — implemented in Plan 02 ──────────────────────────────────────

void MiracastHandler::sendRtspRequest(const QString& method, const QString& uri,
                                       const QString& body) {
    Q_UNUSED(method)
    Q_UNUSED(uri)
    Q_UNUSED(body)
    qDebug("MiracastHandler::sendRtspRequest — not yet implemented (Plan 02)");
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

    QString response = QStringLiteral("RTSP/1.0 %1 %2\r\nCSeq: %3\r\n")
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

void MiracastHandler::onRtspConnected() {
    qDebug("MiracastHandler::onRtspConnected — not yet implemented (Plan 02)");
}

void MiracastHandler::onRtspData() {
    qDebug("MiracastHandler::onRtspData — not yet implemented (Plan 02)");
}

void MiracastHandler::onRtspDisconnected() {
    qDebug("MiracastHandler::onRtspDisconnected — not yet implemented (Plan 02)");
    m_state = State::Idle;
}

} // namespace myairshow
