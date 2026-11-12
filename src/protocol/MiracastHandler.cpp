#include "protocol/MiracastHandler.h"
#include "ui/ConnectionBridge.h"
#include "security/SecurityManager.h"
#include "pipeline/MediaPipeline.h"

// Qt Network
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QPointer>
#include <QTimer>

// Qt Core
#include <QDebug>
#include <QStringConverter>
#include <QtEndian>

namespace airshow {

namespace {
// Close a socket after its queued writes are sent, then delete it; sets it to
// nullptr. disconnectFromHost() + an immediate deleteLater() would discard the
// write buffer, losing e.g. the TEARDOWN request and the 200 OK to the
// source's TEARDOWN. Deleted after at most 3 s if the peer never reads.
void closeGracefully(QTcpSocket*& socket, QObject* owner) {
    QTcpSocket* s = socket;
    socket = nullptr;
    if (!s) return;
    s->disconnect(owner);
    QObject::connect(s, &QAbstractSocket::disconnected, s, &QObject::deleteLater);
    QTimer::singleShot(3000, s, &QObject::deleteLater);
    s->disconnectFromHost();
    if (s->state() == QAbstractSocket::UnconnectedState) {
        s->deleteLater();
    }
}
} // namespace

// ── MiracastHandler ──────────────────────────────────────────────────────────

MiracastHandler::MiracastHandler(ConnectionBridge* connectionBridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(connectionBridge)
    , m_state(State::Idle)
{
    m_healthTimer.setInterval(1000);
    connect(&m_healthTimer, &QTimer::timeout, this, &MiracastHandler::checkStreamHealth);
}

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
    m_healthTimer.stop();

    // Tear down RTSP client connection
    if (m_rtspSocket) {
        closeGracefully(m_rtspSocket, this);
    }

    // Close accepted MS-MICE client connection
    if (m_miceClient) {
        closeGracefully(m_miceClient, this);
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

    const QHostAddress peer = socket->peerAddress();
    qDebug("MiracastHandler: MS-MICE connection from %s:%d",
           qPrintable(peer.toString()), socket->peerPort());

    // Single-session model. While a session is set up or streaming, only its own
    // source may start over (e.g. Windows reconnecting after a network blip).
    // Anyone else is turned away: otherwise any host on the network could break
    // the session, or have an approval meant for another host apply to itself.
    const bool sessionActive = m_state != State::Idle && m_state != State::WaitingSourceReady;
    if (sessionActive) {
        if (!peer.isEqual(m_sourceAddr, QHostAddress::ConvertV4MappedToIPv4)) {
            qWarning("MiracastHandler: busy with %s — rejecting MS-MICE connection from %s",
                     qPrintable(m_sourceAddr.toString()), qPrintable(peer.toString()));
            closeGracefully(socket, this);
            return;
        }
        qDebug("MiracastHandler: source reconnected — restarting the session");
        teardown();
    }

    if (m_miceClient) {
        // A connection that hasn't been approved yet: replace it. Its pending
        // approval (if any) is discarded in handleSourceReady's callback.
        closeGracefully(m_miceClient, this);
    }

    m_sourceAddr = peer;
    m_miceClient = socket;
    m_miceBuffer.clear();
    connect(m_miceClient, &QTcpSocket::readyRead,
            this, &MiracastHandler::onMiceData);

    m_state = State::WaitingSourceReady;
}

void MiracastHandler::onMiceData() {
    if (!m_miceClient) return;
    m_miceBuffer.append(m_miceClient->readAll());

    // TCP is a byte stream: frame messages using the 2-byte Size header.
    for (;;) {
        const int len = miceMessageLength(m_miceBuffer);
        if (len == 0) return;  // need more data
        if (len < 0) {
            qWarning("MiracastHandler: invalid MS-MICE framing — dropping %lld buffered bytes",
                     static_cast<long long>(m_miceBuffer.size()));
            m_miceBuffer.clear();
            return;
        }
        const QByteArray message = m_miceBuffer.left(len);
        m_miceBuffer.remove(0, len);
        parseMiceMessage(message);
        if (!m_miceClient) return;  // message handling may have torn the session down
    }
}

// static
int MiracastHandler::miceMessageLength(const QByteArray& buf) {
    constexpr int kHeaderSize = 4;  // Size(2) + Version(1) + Command(1)
    if (buf.size() < 2) return 0;
    const auto* b = reinterpret_cast<const uint8_t*>(buf.constData());
    const int size = (static_cast<int>(b[0]) << 8) | b[1];
    if (size < kHeaderSize) return -1;
    return buf.size() >= size ? size : 0;
}

void MiracastHandler::parseMiceMessage(const QByteArray& data) {
    if (data.size() < 4) return;
    const uint8_t command = static_cast<uint8_t>(data[3]);

    switch (command) {
    case kMiceSourceReady:
        handleSourceReady(data);
        break;
    case kMiceStopProjection:
        qDebug("MiracastHandler: STOP_PROJECTION received from source");
        if (m_state != State::WaitingSourceReady && m_state != State::Idle) {
            teardown();
        }
        break;
    default:
        // SECURITY_HANDSHAKE / SESSION_REQUEST / PIN_* are optional and not supported.
        qDebug("MiracastHandler: ignoring MS-MICE command 0x%02X (%lld bytes)",
               command, static_cast<long long>(data.size()));
        break;
    }
}

void MiracastHandler::handleSourceReady(const QByteArray& data) {
    // Delegate to the static parser for testability
    SourceReadyInfo info;
    if (!parseSourceReady(data, info)) {
        qWarning("MiracastHandler: failed to parse SOURCE_READY (%lld bytes)",
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
        // The approval is for this connection only: if it was replaced (another
        // MS-MICE connection arrived while the prompt was open) or a session has
        // started meanwhile, drop the answer.
        QPointer<MiracastHandler> self(this);
        QPointer<QTcpSocket>      client(m_miceClient);
        m_securityManager->checkConnectionAsync(
            m_sourceName, QStringLiteral("miracast"), m_sourceAddr.toString(),
            [self, client](bool approved) {
                if (!self || !client || client != self->m_miceClient ||
                    self->m_state != State::WaitingSourceReady) {
                    qDebug("MiracastHandler: ignoring approval for a connection that is gone");
                    return;
                }
                MiracastHandler* const h = self;
                if (!approved) {
                    qDebug("MiracastHandler: connection denied by security manager");
                    client->disconnectFromHost();
                    return;
                }
                h->connectToSource();
            });
    } else {
        // No security manager — connect directly (backward compatible)
        connectToSource();
    }
}

// static
bool MiracastHandler::parseSourceReady(const QByteArray& data, SourceReadyInfo& out) {
    // [MS-MICE] 2.2 message format (all multi-byte fields big-endian):
    //   Size    (2 bytes): size of the whole message, header included
    //   Version (1 byte):  0x01
    //   Command (1 byte):  0x01 = SOURCE_READY
    //   TLVArray, each TLV ([MS-MICE] 2.2.7):
    //     Type   (1 byte)
    //     Length (2 bytes): length of Value
    //     Value  (Length bytes)
    //   SOURCE_READY TLVs:
    //     0x00 FRIENDLY_NAME: UTF-16LE string, no terminator
    //     0x02 RTSP_PORT:     uint16
    //     0x03 SOURCE_ID:     16 bytes

    constexpr int kHeaderSize = 4;
    if (data.size() < kHeaderSize) {
        return false;
    }

    const auto* buf = reinterpret_cast<const uint8_t*>(data.constData());
    const int     msgSize = (static_cast<int>(buf[0]) << 8) | buf[1];
    const uint8_t version = buf[2];
    const uint8_t command = buf[3];

    if (version != 0x01) {
        qWarning("MiracastHandler: unexpected MS-MICE version 0x%02X", version);
        // Continue parsing anyway — be lenient
    }
    if (command != kMiceSourceReady) {
        qWarning("MiracastHandler: unexpected MS-MICE command 0x%02X (expected 0x01 SOURCE_READY)",
                 command);
        return false;
    }

    // Clamp parse range to actual buffer size (guard against truncated messages)
    const int parseLen = qMin(msgSize, static_cast<int>(data.size()));

    int pos = kHeaderSize;
    while (pos + 3 <= parseLen) {
        const uint8_t  tlvType = buf[pos];
        const int      tlvLen  = (static_cast<int>(buf[pos + 1]) << 8) | buf[pos + 2];
        pos += 3;

        if (pos + tlvLen > parseLen) {
            qWarning("MiracastHandler: TLV 0x%02X length %d exceeds message bounds",
                     tlvType, tlvLen);
            break;
        }

        const QByteArray tlvValue(reinterpret_cast<const char*>(buf + pos), tlvLen);
        pos += tlvLen;

        switch (tlvType) {
        case 0x00: {
            auto decoder = QStringDecoder(QStringDecoder::Utf16LE);
            out.friendlyName = decoder.decode(tlvValue);
            break;
        }
        case 0x02:
            if (tlvLen >= 2) {
                out.rtspPort = static_cast<uint16_t>(
                    (static_cast<uint8_t>(tlvValue[0]) << 8) |
                     static_cast<uint8_t>(tlvValue[1]));
            }
            break;
        case 0x03:
            out.sourceId = QString::fromLatin1(tlvValue.toHex());
            break;
        default:
            qDebug("MiracastHandler: ignoring TLV type 0x%02X len=%d", tlvType, tlvLen);
            break;
        }
    }

    return true;
}

// ── RTSP connection ───────────────────────────────────────────────────────────

void MiracastHandler::connectToSource() {
    // Create RTSP client socket and connect to source:7236
    if (m_rtspSocket) {
        closeGracefully(m_rtspSocket, this);
    }

    m_rtspBuffer.clear();
    m_cseq = 1;
    m_presentationUrl.clear();
    m_sessionId.clear();
    m_m2Cseq = m_setupCseq = m_playCseq = -1;

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

    // Process all complete messages in the buffer. The WFD sink reacts to each
    // message by its method (requests) or by the CSeq of the request it answers
    // (responses), so keepalives, triggers and TEARDOWN work in any state.
    RtspMessage msg;
    while (m_rtspSocket && parseNextRtspMessage(msg)) {
        qDebug("Miracast: RTSP received [%s] state=%d cseq=%d status=%d",
               msg.isRequest ? qPrintable(msg.method) : "response",
               static_cast<int>(m_state),
               msg.cseq,
               msg.statusCode);

        if (msg.isRequest) {
            handleRtspRequest(msg);
        } else {
            handleRtspResponse(msg);
        }
    }
}

void MiracastHandler::handleRtspRequest(const RtspMessage& msg) {
    if (msg.method == QStringLiteral("OPTIONS")) {
        // M1: OPTIONS from source with "Require: org.wfa.wfd1.0"
        const QString m1Resp =
            QStringLiteral("RTSP/1.0 200 OK\r\n"
                           "CSeq: %1\r\n"
                           "Server: AirShow/1.0\r\n"
                           "Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, "
                           "SETUP, PLAY, PAUSE, TEARDOWN\r\n"
                           "\r\n")
            .arg(msg.cseq);
        m_rtspSocket->write(m1Resp.toUtf8());
        qDebug("Miracast: sent M1 200 OK");

        // M2: our OPTIONS to the source (RESEARCH.md Pitfall 3 — M2 is NOT optional)
        if (m_m2Cseq < 0) {
            m_m2Cseq = sendRtspRequest(QStringLiteral("OPTIONS"), QStringLiteral("*"),
                                       QStringLiteral("Require: org.wfa.wfd1.0\r\n"));
            m_state = State::NegotiatingM2;
            qDebug("Miracast: sent M2 OPTIONS");
        }
        return;
    }

    if (msg.method == QStringLiteral("GET_PARAMETER")) {
        if (msg.body.trimmed().isEmpty()) {
            // M16 keepalive: empty GET_PARAMETER
            m_rtspSocket->write(buildRtspResponse(msg.cseq, 200).toUtf8());
            qDebug("Miracast: keepalive GET_PARAMETER acknowledged");
        } else {
            // M3: capability query
            const QString capResp = QString::fromUtf8(kWfdCapabilityResponse);
            m_rtspSocket->write(buildRtspResponse(msg.cseq, 200, capResp).toUtf8());
            m_state = State::NegotiatingM4;
            qDebug("Miracast: sent M3 capability response");
        }
        return;
    }

    if (msg.method == QStringLiteral("SET_PARAMETER")) {
        m_rtspSocket->write(buildRtspResponse(msg.cseq, 200).toUtf8());

        const QString url = parsePresentationUrl(msg.body);
        if (!url.isEmpty()) {
            // M4: selected parameters
            m_presentationUrl = url;
            qDebug("Miracast: M4 SET_PARAMETER received — selected params:\n%s",
                   msg.body.constData());
            m_state = State::NegotiatingM5;
            qDebug("Miracast: sent M4 200 OK");
        }

        // M5 (or later trigger): "wfd_trigger_method: SETUP|PLAY|PAUSE|TEARDOWN"
        const QString method = parseTriggerMethod(msg.body);
        if (!method.isEmpty()) {
            qDebug("Miracast: trigger %s received", qPrintable(method));
            if (method == QStringLiteral("SETUP")) {
                sendSetup();
            } else if (method == QStringLiteral("PLAY")) {
                sendPlay();
            } else if (method == QStringLiteral("PAUSE")) {
                // Ask the source to pause (WFD M9). The receive pipeline is left
                // PLAYING on purpose: it is live, simply gets no packets while
                // paused, and resumes at once on the PLAY trigger that follows.
                // The PAUSE response needs no handling (logged as unmatched).
                sendRtspRequest(QStringLiteral("PAUSE"), streamUri(),
                                QStringLiteral("Session: %1\r\n").arg(m_sessionId));
            } else if (method == QStringLiteral("TEARDOWN")) {
                sendRtspRequest(QStringLiteral("TEARDOWN"), streamUri(),
                                QStringLiteral("Session: %1\r\n").arg(m_sessionId));
                teardown();
            }
        } else if (url.isEmpty()) {
            qDebug("Miracast: SET_PARAMETER acknowledged:\n%s", msg.body.constData());
        }
        return;
    }

    if (msg.method == QStringLiteral("TEARDOWN")) {
        m_rtspSocket->write(buildRtspResponse(msg.cseq, 200).toUtf8());
        qDebug("Miracast: TEARDOWN received from source");
        teardown();
        return;
    }

    qDebug("Miracast: unsupported RTSP request %s — replying 501", qPrintable(msg.method));
    m_rtspSocket->write(
        QStringLiteral("RTSP/1.0 501 Not Implemented\r\nCSeq: %1\r\n\r\n")
            .arg(msg.cseq).toUtf8());
}

void MiracastHandler::handleRtspResponse(const RtspMessage& msg) {
    if (msg.cseq == m_m2Cseq) {
        if (msg.statusCode == 200) {
            m_state = State::NegotiatingM3;
            qDebug("Miracast: M2 acknowledged — waiting for M3 GET_PARAMETER");
        } else {
            qWarning("Miracast: M2 OPTIONS failed with status %d", msg.statusCode);
        }
        return;
    }

    if (msg.cseq == m_setupCseq) {
        if (msg.statusCode != 200) {
            qWarning("Miracast: M6 SETUP failed with status %d", msg.statusCode);
            teardown();
            return;
        }
        m_sessionId = parseSessionId(msg.session);
        qDebug("Miracast: M6 SETUP accepted (session '%s') — sending M7 PLAY",
               qPrintable(m_sessionId));

        // Initialize the GStreamer MPEG-TS/RTP receive pipeline
        if (m_pipeline) {
            if (!m_pipeline->initMiracastPipeline(m_qmlVideoItem, m_udpPort)) {
                qWarning("Miracast: failed to initialize media pipeline");
            }
        }
        sendPlay();
        return;
    }

    if (msg.cseq == m_playCseq) {
        if (msg.statusCode != 200) {
            qWarning("Miracast: M7 PLAY failed with status %d", msg.statusCode);
            teardown();
            return;
        }
        if (m_state == State::Streaming) return;  // PLAY after a PAUSE trigger

        // Start the Miracast receive pipeline (it was created PAUSED at M6)
        if (m_pipeline) {
            m_pipeline->playMiracast();
        }

        // Update HUD (D-16)
        if (m_connectionBridge) {
            m_connectionBridge->setConnected(true, m_sourceName,
                                             QStringLiteral("miracast"));
        }

        m_state = State::Streaming;
        m_lastStreamErrors = 0;
        m_healthTicks = 0;
        m_idrInterval = 1;
        m_nextIdrTick = 1;
        m_healthTimer.start();
        qDebug("Miracast: streaming from '%s'", qPrintable(m_sourceName));
        return;
    }

    qDebug("Miracast: response cseq=%d status=%d (no pending request)",
           msg.cseq, msg.statusCode);
}

QString MiracastHandler::streamUri() const {
    if (!m_presentationUrl.isEmpty()) {
        return m_presentationUrl;
    }
    // Fallback: build from the source address. Unmap IPv4-mapped IPv6
    // (e.g. "::ffff:192.168.1.5") so the URI host is a plain dotted quad.
    bool isV4 = false;
    const quint32 v4 = m_sourceAddr.toIPv4Address(&isV4);
    const QString host = isV4 ? QHostAddress(v4).toString()
                              : QStringLiteral("[%1]").arg(m_sourceAddr.toString());
    return QStringLiteral("rtsp://%1/wfd1.0/streamid=0").arg(host);
}

void MiracastHandler::sendIdrRequest() {
    // Same request lazycast sends; Windows answers with a fresh IDR frame.
    sendRtspRequest(QStringLiteral("SET_PARAMETER"), QStringLiteral("rtsp://localhost/wfd1.0"),
                    {}, QStringLiteral("wfd_idr_request\r\n"));
}

void MiracastHandler::checkStreamHealth() {
    if (m_state != State::Streaming || !m_pipeline || !m_rtspSocket) {
        m_healthTimer.stop();
        return;
    }
    const uint64_t frames = m_pipeline->miracastFramesDecoded();
    const uint64_t errors = m_pipeline->miracastStreamErrors();
    ++m_healthTicks;

    if (frames == 0) {
        // Nothing decoded yet: the first IDR may have been lost. Ask again with
        // exponential backoff (1, 2, 4, 8, 16, 16, ... s) rather than every second,
        // since a missing decoder or an audio-only stream will never produce frames.
        if (m_healthTicks >= m_nextIdrTick) {
            qDebug("Miracast: no video frame decoded yet — requesting IDR");
            sendIdrRequest();
            m_idrInterval = qMin(m_idrInterval * 2, kMaxIdrIntervalSec);
            m_nextIdrTick = m_healthTicks + m_idrInterval;
        }
        if (m_healthTicks == kNoVideoWarnSec) {
            qWarning("Miracast: no video decoded after %d s — check the H.264 decoder "
                     "(avdec_h264 / vaapidecodebin) and the pipeline warnings above",
                     kNoVideoWarnSec);
        }
    } else if (errors != m_lastStreamErrors) {
        // At most one request per tick (1 s), however many errors arrived
        qDebug("Miracast: stream errors (%llu total) — requesting IDR",
               static_cast<unsigned long long>(errors));
        sendIdrRequest();
    }
    m_lastStreamErrors = errors;
}

void MiracastHandler::sendSetup() {
    const QString transportHeader =
        QStringLiteral("Transport: RTP/AVP/UDP;unicast;client_port=%1\r\n").arg(m_udpPort);
    m_setupCseq = sendRtspRequest(QStringLiteral("SETUP"), streamUri(), transportHeader);
    m_state = State::SendingSetup;
    qDebug("Miracast: sent M6 SETUP %s (client_port=%d)", qPrintable(streamUri()), m_udpPort);
}

void MiracastHandler::sendPlay() {
    // RFC 2326: PLAY must carry the Session id returned by SETUP
    const QString sessionHeader = m_sessionId.isEmpty()
        ? QString() : QStringLiteral("Session: %1\r\n").arg(m_sessionId);
    m_playCseq = sendRtspRequest(QStringLiteral("PLAY"), streamUri(), sessionHeader);
    if (m_state != State::Streaming) {
        m_state = State::SendingPlay;
    }
    qDebug("Miracast: sent M7 PLAY");
}

// static
QString MiracastHandler::parsePresentationUrl(const QByteArray& body) {
    for (const QByteArray& rawLine : body.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (!line.startsWith("wfd_presentation_URL:")) continue;
        const QList<QByteArray> parts =
            line.mid(int(sizeof("wfd_presentation_URL:")) - 1).simplified().split(' ');
        if (parts.isEmpty() || parts[0].isEmpty() || parts[0] == "none") return {};
        return QString::fromUtf8(parts[0]);
    }
    return {};
}

// static
QString MiracastHandler::parseTriggerMethod(const QByteArray& body) {
    static constexpr char kKey[] = "wfd_trigger_method:";
    for (const QByteArray& rawLine : body.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith(kKey)) {
            return QString::fromUtf8(line.mid(int(sizeof(kKey)) - 1)).trimmed().toUpper();
        }
    }
    return {};
}

// static
QString MiracastHandler::parseSessionId(const QString& sessionHeader) {
    return sessionHeader.section(QLatin1Char(';'), 0, 0).trimmed();
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
        if (m_pipeline) {
            m_pipeline->stopMiracast();
        }
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
    m_healthTimer.stop();
    qDebug("Miracast: session ended");

    if (m_pipeline) {
        m_pipeline->stopMiracast();
    }

    if (m_connectionBridge) {
        m_connectionBridge->setConnected(false);
    }

    if (m_rtspSocket) {
        closeGracefully(m_rtspSocket, this);
    }

    if (m_miceClient) {
        closeGracefully(m_miceClient, this);
    }

    m_rtspBuffer.clear();
    m_miceBuffer.clear();
    m_sourceName.clear();
    m_sourceId.clear();
    m_presentationUrl.clear();
    m_sessionId.clear();
    m_m2Cseq = m_setupCseq = m_playCseq = -1;
    m_cseq = 1;

    // Return to waiting for next connection (server still listening)
    if (m_running) {
        m_state = State::WaitingSourceReady;
    } else {
        m_state = State::Idle;
    }
}

int MiracastHandler::sendRtspRequest(const QString& method, const QString& uri,
                                      const QString& extraHeaders, const QString& body) {
    if (!m_rtspSocket) {
        qWarning("MiracastHandler::sendRtspRequest called with no RTSP socket");
        return -1;
    }

    const int cseq = m_cseq++;
    QString request = QStringLiteral("%1 %2 RTSP/1.0\r\nCSeq: %3\r\n")
        .arg(method)
        .arg(uri)
        .arg(cseq);

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
    return cseq;
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

    QString response = QStringLiteral("RTSP/1.0 %1 %2\r\nCSeq: %3\r\nServer: AirShow/1.0\r\n")
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
        } else if (key == QStringLiteral("session")) {
            msg.session = value;
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

} // namespace airshow
