#include "protocol/AirShowHandler.h"
#include "ui/ConnectionBridge.h"
#include "security/SecurityManager.h"
#include "pipeline/MediaPipeline.h"

// Qt Network
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

// Qt Core
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

// GStreamer
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

namespace airshow {

// ── AirShowHandler ────────────────────────────────────────────────────────────

AirShowHandler::AirShowHandler(ConnectionBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(bridge)
    , m_state(State::Idle)
{}

AirShowHandler::~AirShowHandler() {
    stop();
}

bool AirShowHandler::start() {
    if (m_running) return true;

    m_server = new QTcpServer(this);

    connect(m_server, &QTcpServer::newConnection,
            this, &AirShowHandler::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, kAirShowPort)) {
        qCritical("AirShowHandler: failed to listen on port %u: %s",
                  static_cast<unsigned>(kAirShowPort),
                  qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_running = true;
    qDebug("AirShowHandler: listening on port %u", static_cast<unsigned>(kAirShowPort));
    return true;
}

void AirShowHandler::stop() {
    if (!m_running) return;

    if (m_client) {
        disconnectClient();
    }

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    m_running = false;
}

void AirShowHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void AirShowHandler::setSecurityManager(SecurityManager* sm) {
    m_security = sm;
}

// ── Static frame header parser ────────────────────────────────────────────────

bool AirShowHandler::parseFrameHeader(const QByteArray& data, FrameHeader& out) {
    if (data.size() < kFrameHeaderSize)
        return false;

    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());

    out.type   = static_cast<uint8_t>(bytes[0]);
    out.flags  = static_cast<uint8_t>(bytes[1]);
    out.length = qFromBigEndian<quint32>(bytes + 2);
    out.pts    = qFromBigEndian<qint64>(bytes + 6);

    return true;
}

// ── Qt slots ──────────────────────────────────────────────────────────────────

void AirShowHandler::onNewConnection() {
    QTcpSocket* incoming = m_server->nextPendingConnection();
    if (!incoming)
        return;

    // Single-session model: disconnect any existing client before accepting new one
    if (m_client) {
        qInfo("AirShowHandler: dropping existing connection to accept new one");
        disconnectClient();
    }

    m_client = incoming;
    connect(m_client, &QTcpSocket::readyRead,
            this, &AirShowHandler::onReadyRead);
    connect(m_client, &QTcpSocket::disconnected,
            this, &AirShowHandler::onDisconnected);

    m_state = State::Handshake;
    m_readBuffer.clear();

    qInfo("AirShowHandler: new connection from %s",
          qPrintable(m_client->peerAddress().toString()));
}

void AirShowHandler::onReadyRead() {
    if (!m_client)
        return;

    m_readBuffer.append(m_client->readAll());

    if (m_state == State::Handshake) {
        handleHandshake();
    } else if (m_state == State::Streaming) {
        handleStreamingData();
    }
}

void AirShowHandler::onDisconnected() {
    qInfo("AirShowHandler: client disconnected");
    disconnectClient();
}

// ── Protocol handling ─────────────────────────────────────────────────────────

void AirShowHandler::handleHandshake() {
    // Wait for a complete newline-terminated JSON line
    const int newlineIdx = m_readBuffer.indexOf('\n');
    if (newlineIdx < 0)
        return;  // incomplete — wait for more data

    QByteArray jsonLine = m_readBuffer.left(newlineIdx);
    m_readBuffer.remove(0, newlineIdx + 1);  // consume including newline

    // Parse the HELLO JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonLine, &parseError);

    if (doc.isNull() || !doc.isObject()) {
        qWarning("AirShowHandler: invalid HELLO JSON (parse error: %s) — disconnecting",
                 qPrintable(parseError.errorString()));
        disconnectClient();
        return;
    }

    QJsonObject obj = doc.object();

    // Validate type and version
    if (obj[QStringLiteral("type")].toString() != QStringLiteral("HELLO") ||
        obj[QStringLiteral("version")].toInt() != 1)
    {
        qWarning("AirShowHandler: unexpected handshake message type='%s' version=%d — disconnecting",
                 qPrintable(obj[QStringLiteral("type")].toString()),
                 obj[QStringLiteral("version")].toInt());
        disconnectClient();
        return;
    }

    // Extract client metadata
    m_clientDeviceName = obj[QStringLiteral("deviceName")].toString();

    // Build HELLO_ACK — echo back the negotiated quality parameters
    // (for now we accept what the sender requests unchanged)
    QJsonObject ack;
    ack[QStringLiteral("type")]               = QStringLiteral("HELLO_ACK");
    ack[QStringLiteral("version")]            = 1;
    ack[QStringLiteral("codec")]              = QStringLiteral("h264");  // only supported codec
    ack[QStringLiteral("acceptedResolution")] = obj[QStringLiteral("maxResolution")];
    ack[QStringLiteral("acceptedBitrate")]    = obj[QStringLiteral("targetBitrate")];
    ack[QStringLiteral("acceptedFps")]        = obj[QStringLiteral("fps")];

    QByteArray ackBytes = QJsonDocument(ack).toJson(QJsonDocument::Compact);
    ackBytes.append('\n');
    m_client->write(ackBytes);
    m_client->flush();

    // Transition to streaming state
    m_state = State::Streaming;

    // Initialize the appsrc pipeline if not already running
    if (m_pipeline && !m_pipeline->videoAppsrc()) {
        m_pipeline->initAppsrcPipeline(nullptr);
    }

    // Update the connection HUD
    if (m_connectionBridge) {
        m_connectionBridge->setConnected(true, m_clientDeviceName,
                                         QStringLiteral("airshow"));
    }

    qInfo("AirShowHandler: handshake complete — device='%s', streaming on port %u",
          qPrintable(m_clientDeviceName), static_cast<unsigned>(kAirShowPort));

    // If bytes already arrived during handshake, process them now
    if (!m_readBuffer.isEmpty()) {
        handleStreamingData();
    }
}

void AirShowHandler::handleStreamingData() {
    // Process all complete frames from the read buffer
    while (m_readBuffer.size() >= kFrameHeaderSize) {
        // Read the payload length from the header (big-endian uint32 at offset 2)
        const auto* bytes = reinterpret_cast<const uchar*>(m_readBuffer.constData());
        quint32 payloadLen = qFromBigEndian<quint32>(bytes + 2);

        // Wait until we have the complete frame (header + payload)
        if (m_readBuffer.size() < kFrameHeaderSize + static_cast<int>(payloadLen))
            break;

        QByteArray frameData = m_readBuffer.left(kFrameHeaderSize + static_cast<int>(payloadLen));
        m_readBuffer.remove(0, kFrameHeaderSize + static_cast<int>(payloadLen));

        processFrame(frameData);
    }
}

void AirShowHandler::processFrame(const QByteArray& frameData) {
    FrameHeader header;
    if (!parseFrameHeader(frameData, header)) {
        qWarning("AirShowHandler: processFrame called with too-short data (%lld bytes)",
                 static_cast<long long>(frameData.size()));
        return;
    }

    switch (header.type) {
    case kTypeVideoNal:
        if (m_pipeline && m_pipeline->videoAppsrc()) {
            // Allocate a GstBuffer and fill with the NAL payload (after the 16-byte header)
            GstBuffer* buf = gst_buffer_new_allocate(nullptr, header.length, nullptr);
            if (buf) {
                gst_buffer_fill(buf, 0,
                                frameData.constData() + kFrameHeaderSize,
                                header.length);
                GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(header.pts);
                GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
                gst_app_src_push_buffer(GST_APP_SRC(m_pipeline->videoAppsrc()), buf);
            }
        }
        break;

    case kTypeAudio:
        // Audio streaming deferred to Phase 10
        qDebug("AirShowHandler: audio frame received (not yet implemented)");
        break;

    case kTypeKeepalive:
        // No-op: connection stays alive
        break;

    default:
        qWarning("AirShowHandler: unknown frame type 0x%02x", static_cast<unsigned>(header.type));
        break;
    }
}

void AirShowHandler::disconnectClient() {
    if (m_client) {
        m_client->disconnect(this);
        m_client->disconnectFromHost();
        m_client->deleteLater();
        m_client = nullptr;
    }

    m_readBuffer.clear();
    m_state = State::Idle;

    if (m_connectionBridge) {
        m_connectionBridge->setConnected(false);
    }
}

} // namespace airshow
