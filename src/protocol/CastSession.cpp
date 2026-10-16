#include "protocol/CastSession.h"
#include "ui/ConnectionBridge.h"
#include "pipeline/MediaPipeline.h"
#include "security/SecurityManager.h"
#include "cast/cast_auth_sigs.h"
#include "cast/CastStreamingOffer.h"
#include "cast/CastStreamingReceiver.h"

// Generated protobuf headers (from cast_channel.proto via protobuf_generate_cpp)
#include "cast_channel.pb.h"

// Qt
#include <QSslSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QUuid>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QtEndian>
#include <QDebug>

// std
#include <cstring>

using extensions::api::cast_channel::CastMessage;
using extensions::api::cast_channel::DeviceAuthMessage;
using extensions::api::cast_channel::AuthResponse;

namespace airshow {

// ── Namespace constants ────────────────────────────────────────────────────────

static constexpr const char* kNsDeviceAuth  = "urn:x-cast:com.google.cast.tp.deviceauth";
static constexpr const char* kNsConnection  = "urn:x-cast:com.google.cast.tp.connection";
static constexpr const char* kNsHeartbeat   = "urn:x-cast:com.google.cast.tp.heartbeat";
static constexpr const char* kNsReceiver    = "urn:x-cast:com.google.cast.receiver";
static constexpr const char* kNsMedia       = "urn:x-cast:com.google.cast.media";
static constexpr const char* kNsWebrtc      = "urn:x-cast:com.google.cast.webrtc";

// Chrome Mirroring app IDs accepted at LAUNCH
static constexpr const char* kAppIdChromeMirror = "0F5096E8";
static constexpr const char* kAppIdDefaultMedia = "CC1AD845";
static constexpr const char* kAppIdAudioMirror  = "85CDB22F";  // audio-only mirroring

// ── CastSession ───────────────────────────────────────────────────────────────

CastSession::CastSession(QSslSocket* socket,
                          ConnectionBridge* connectionBridge,
                          MediaPipeline* pipeline,
                          SecurityManager* securityManager,
                          QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_connectionBridge(connectionBridge)
    , m_pipeline(pipeline)
    , m_securityManager(securityManager)
    , m_approved(securityManager == nullptr)
{
    // Session owns the socket
    m_socket->setParent(this);

    // Generate a unique transportId for this session (used in RECEIVER_STATUS)
    m_transportId = QStringLiteral("cast-transport-") +
                    QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    // Wire up socket signals
    connect(m_socket, &QSslSocket::readyRead,
            this, &CastSession::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected,
            this, &CastSession::onDisconnected);

    qDebug("CastSession: created with transportId=%s", qPrintable(m_transportId));
}

CastSession::~CastSession() {
    stopStreaming();
    if (m_socket && m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

// ── TCP framing state machine ─────────────────────────────────────────────────

void CastSession::onReadyRead() {
    // Append all available data to accumulation buffer (non-blocking, per Pitfall 6)
    m_buffer.append(m_socket->readAll());

    // Process as many complete messages as available
    while (true) {
        if (m_readState == ReadState::READING_HEADER) {
            // Need at least 4 bytes for the length prefix
            if (m_buffer.size() < 4) break;

            // Extract big-endian uint32 length
            uint32_t lenBe;
            std::memcpy(&lenBe, m_buffer.constData(), 4);
            m_expectedLen = qFromBigEndian<uint32_t>(lenBe);
            m_buffer.remove(0, 4);
            m_readState = ReadState::READING_PAYLOAD;
        }

        if (m_readState == ReadState::READING_PAYLOAD) {
            if (static_cast<uint32_t>(m_buffer.size()) < m_expectedLen) break;

            // Extract exactly the expected payload bytes
            QByteArray payload = m_buffer.left(static_cast<int>(m_expectedLen));
            m_buffer.remove(0, static_cast<int>(m_expectedLen));
            m_readState = ReadState::READING_HEADER;

            // Parse the protobuf CastMessage
            CastMessage msg;
            if (!msg.ParseFromArray(payload.constData(), payload.size())) {
                qWarning("CastSession: failed to parse CastMessage (%lld bytes)",
                         static_cast<long long>(payload.size()));
                continue;
            }

            dispatchMessage(msg);
        }
    }
}

// ── Message dispatch ──────────────────────────────────────────────────────────

// True for messages that start or control an actual cast session. Everything
// else (auth, heartbeat, receiver-0 CONNECT/CLOSE, GET_STATUS) is what Chrome
// does for every receiver it discovers and must not trigger an approval prompt.
static bool requiresApproval(const CastMessage& msg) {
    const std::string& ns = msg.namespace_();
    if (ns == kNsDeviceAuth || ns == kNsHeartbeat) return false;
    if (ns == kNsMedia || ns == kNsWebrtc) return true;
    if (ns != kNsConnection && ns != kNsReceiver) return false;
    if (msg.payload_type() != CastMessage::STRING) return false;

    const QString type = QJsonDocument::fromJson(QByteArray::fromStdString(msg.payload_utf8()))
                             .object().value(QStringLiteral("type")).toString();
    if (ns == kNsConnection)
        return type == QStringLiteral("CONNECT") && msg.destination_id() != "receiver-0";
    return type == QStringLiteral("LAUNCH");
}

void CastSession::dispatchMessage(const CastMessage& msg) {
    if (m_rejected) return;

    if (!m_approved && requiresApproval(msg)) {
        std::string serialized;
        msg.SerializeToString(&serialized);
        m_heldMessages.push_back(std::move(serialized));
        requestApproval();
        return;
    }

    const std::string& ns = msg.namespace_();

    if (ns == kNsDeviceAuth) {
        onDeviceAuth(msg);
    } else if (ns == kNsConnection) {
        onConnection(msg);
    } else if (ns == kNsHeartbeat) {
        onHeartbeat(msg);
    } else if (ns == kNsReceiver) {
        onReceiver(msg);
    } else if (ns == kNsMedia) {
        onMedia(msg);
    } else if (ns == kNsWebrtc) {
        onWebrtc(msg);
    } else {
        qWarning("CastSession: unknown namespace '%s' — ignoring", ns.c_str());
    }
}

void CastSession::requestApproval() {
    if (m_approvalPending) return;
    m_approvalPending = true;

    const QString name = m_senderName.isEmpty() ? QStringLiteral("Cast Sender") : m_senderName;
    const QString peerIp = m_socket ? m_socket->peerAddress().toString()
                                    : QStringLiteral("unknown");
    // The session can be destroyed (sender disconnects) while the prompt is open.
    QPointer<CastSession> self(this);
    m_securityManager->checkConnectionAsync(
        name, QStringLiteral("Cast"), peerIp,
        [self](bool approved) {
            if (self) self->onApprovalResolved(approved);
        });
}

void CastSession::onApprovalResolved(bool approved) {
    m_approvalPending = false;
    if (!approved) {
        qDebug("CastSession: Cast session denied by security manager");
        m_rejected = true;
        m_heldMessages.clear();
        emit finished();
        return;
    }

    m_approved = true;
    std::vector<std::string> held;
    held.swap(m_heldMessages);
    for (const std::string& bytes : held) {
        CastMessage msg;
        if (msg.ParseFromString(bytes)) dispatchMessage(msg);
    }
}

// ── Namespace handlers ────────────────────────────────────────────────────────

void CastSession::onDeviceAuth(const CastMessage& msg) {
    // Auth challenge is a binary-payload DeviceAuthMessage (RESEARCH.md Pattern 3)
    if (msg.payload_type() != CastMessage::BINARY) {
        qWarning("CastSession: deviceauth message has non-binary payload — ignoring");
        return;
    }

    // Parse the challenge (we ignore the nonce — Chrome has enforce_nonce_checking=false)
    DeviceAuthMessage challenge;
    if (!challenge.ParseFromString(msg.payload_binary())) {
        qWarning("CastSession: failed to parse DeviceAuthMessage");
        return;
    }

    if (!challenge.has_challenge()) {
        qWarning("CastSession: DeviceAuthMessage has no challenge field");
        return;
    }

    qDebug("CastSession: received AuthChallenge, sending precomputed signature response");

    // The signature must match the TLS certificate this socket was handshaked
    // with, so take the window from the cert's notBefore rather than the clock
    // (a connection made just before a window boundary would otherwise mismatch).
    const QSslCertificate localCert = m_socket->sslConfiguration().localCertificate();
    const uint64_t windowSecs = localCert.isNull()
        ? static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch())
        : static_cast<uint64_t>(localCert.effectiveDate().toSecsSinceEpoch());
    const uint8_t* sig = cast::getSignatureForTime(windowSecs);
    if (!sig) {
        qWarning("CastSession: no Cast auth signature for this time (table expired) — "
                 "cannot authenticate sender");
        return;
    }

    // Build AuthResponse with real certificates from shanocast/AirReceiver
    AuthResponse authResponse;
    authResponse.set_signature(
        reinterpret_cast<const char*>(sig),
        cast::kCastAuthSignatureSize);
    // Device auth certificate (signed by Google "Eureka Gen1 ICA")
    authResponse.set_client_auth_certificate(
        reinterpret_cast<const char*>(cast::auth_crt),
        cast::auth_crt_len);
    // Intermediate CA certificate (chain link to Google root)
    authResponse.add_intermediate_certificate(
        std::string(reinterpret_cast<const char*>(cast::intermediate_crt),
                    cast::intermediate_crt_len));
    authResponse.set_signature_algorithm(
        extensions::api::cast_channel::RSASSA_PKCS1v15);
    // SHA256 — confirmed by RSA-decrypting the precomputed signatures: the DigestInfo
    // contains SHA256 OID (2.16.840.1.101.3.4.2.1). Chrome verifies the signature using
    // whatever hash_algorithm we declare, so this must match the actual signing algorithm.
    authResponse.set_hash_algorithm(
        extensions::api::cast_channel::SHA256);
    // Do NOT set sender_nonce — AirReceiver omits it, and Chrome's
    // enforce_nonce_checking is false.

    // Wrap in DeviceAuthMessage
    DeviceAuthMessage responseMsg;
    *responseMsg.mutable_response() = authResponse;

    // Send as binary CastMessage on deviceauth namespace
    std::string responseBytes;
    responseMsg.SerializeToString(&responseBytes);

    CastMessage reply;
    reply.set_protocol_version(CastMessage::CASTV2_1_0);
    reply.set_source_id("receiver-0");
    reply.set_destination_id(msg.source_id());
    reply.set_namespace_(kNsDeviceAuth);
    reply.set_payload_type(CastMessage::BINARY);
    reply.set_payload_binary(responseBytes);

    sendMessage(reply);
}

void CastSession::onConnection(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8()));
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString type = json.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("CONNECT")) {
        // Sender may include userAgent or display name
        m_senderName = json.value(QStringLiteral("userAgent")).toString();
        if (m_senderName.isEmpty()) {
            m_senderName = json.value(QStringLiteral("name")).toString();
        }
        if (m_senderName.isEmpty()) {
            m_senderName = QStringLiteral("Cast Sender");
        }

        // (CONNECTs to our transportId only reach here after approval — see
        // requiresApproval().) If connecting to our transportId (not receiver-0), announce connection
        if (msg.destination_id() == m_transportId.toStdString()) {
            qDebug("CastSession: CONNECT to transportId from '%s'",
                   qPrintable(m_senderName));
            m_didConnect = true;
            if (m_connectionBridge) {
                QMetaObject::invokeMethod(m_connectionBridge, [this]() {
                    m_connectionBridge->setConnected(true, m_senderName,
                                                     QStringLiteral("Cast"));
                }, Qt::QueuedConnection);
            }
        } else {
            qDebug("CastSession: CONNECT from '%s' to receiver-0",
                   qPrintable(m_senderName));
        }
    } else if (type == QStringLiteral("CLOSE")) {
        qDebug("CastSession: received CLOSE, ending session");
        stopStreaming();
        if (m_didConnect && m_connectionBridge) {
            QMetaObject::invokeMethod(m_connectionBridge, [this]() {
                m_connectionBridge->setConnected(false);
            }, Qt::QueuedConnection);
        }
        emit finished();
    }
}

void CastSession::onHeartbeat(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8()));
    if (doc.isNull()) return;

    QString type = doc.object().value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("PING")) {
        // Reply PONG on the same connection (RESEARCH.md Pattern 2)
        QByteArray pong = R"({"type":"PONG"})";
        CastMessage reply = makeJsonMsg(
            QString::fromStdString(msg.destination_id()),
            QString::fromStdString(msg.source_id()),
            QStringLiteral("urn:x-cast:com.google.cast.tp.heartbeat"),
            pong);
        sendMessage(reply);
    }
}

void CastSession::onReceiver(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8()));
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString type = json.value(QStringLiteral("type")).toString();
    int requestId = json.value(QStringLiteral("requestId")).toInt(0);

    if (type == QStringLiteral("GET_STATUS")) {
        QByteArray status = buildReceiverStatus(requestId);
        CastMessage reply = makeJsonMsg(
            QStringLiteral("receiver-0"),
            QString::fromStdString(msg.source_id()),
            QStringLiteral("urn:x-cast:com.google.cast.receiver"),
            status);
        sendMessage(reply);

    } else if (type == QStringLiteral("LAUNCH")) {
        QString appId = json.value(QStringLiteral("appId")).toString();

        if (appId == QLatin1StringView(kAppIdChromeMirror) ||
            appId == QLatin1StringView(kAppIdAudioMirror) ||
            appId == QLatin1StringView(kAppIdDefaultMedia)) {

            m_appLaunched = true;
            m_launchedAppId = appId;
            qDebug("CastSession: LAUNCH app %s, transportId=%s",
                   qPrintable(appId), qPrintable(m_transportId));

            QByteArray status = buildReceiverStatus(requestId);
            CastMessage reply = makeJsonMsg(
                QStringLiteral("receiver-0"),
                QString::fromStdString(msg.source_id()),
                QStringLiteral("urn:x-cast:com.google.cast.receiver"),
                status);
            sendMessage(reply);
        } else {
            qDebug("CastSession: LAUNCH unknown appId '%s' — sending empty status",
                   qPrintable(appId));
            QByteArray status = buildReceiverStatus(requestId);
            CastMessage reply = makeJsonMsg(
                QStringLiteral("receiver-0"),
                QString::fromStdString(msg.source_id()),
                QStringLiteral("urn:x-cast:com.google.cast.receiver"),
                status);
            sendMessage(reply);
        }

    } else if (type == QStringLiteral("STOP")) {
        m_appLaunched = false;
        m_launchedAppId.clear();
        qDebug("CastSession: STOP — app stopped");
        stopStreaming();

        QByteArray status = buildReceiverStatus(requestId);
        CastMessage reply = makeJsonMsg(
            QStringLiteral("receiver-0"),
            QString::fromStdString(msg.source_id()),
            QStringLiteral("urn:x-cast:com.google.cast.receiver"),
            status);
        sendMessage(reply);
    }
}

void CastSession::onMedia(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8()));
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString type = json.value(QStringLiteral("type")).toString();
    int requestId = json.value(QStringLiteral("requestId")).toInt(0);

    if (type == QStringLiteral("GET_STATUS")) {
        // Reply with empty media status
        QJsonObject status;
        status[QStringLiteral("type")] = QStringLiteral("MEDIA_STATUS");
        status[QStringLiteral("status")] = QJsonArray();
        status[QStringLiteral("requestId")] = requestId;

        QByteArray payload = QJsonDocument(status).toJson(QJsonDocument::Compact);
        CastMessage reply = makeJsonMsg(
            m_transportId,
            QString::fromStdString(msg.source_id()),
            QStringLiteral("urn:x-cast:com.google.cast.media"),
            payload);
        sendMessage(reply);

    } else if (type == QStringLiteral("LOAD")) {
        // Extract content URL from media.contentId (per D-10)
        QJsonObject mediaObj = json.value(QStringLiteral("media")).toObject();
        QString contentId = mediaObj.value(QStringLiteral("contentId")).toString();

        if (contentId.isEmpty()) {
            qWarning("CastSession: LOAD missing contentId — sending INVALID_REQUEST");
            QJsonObject errorStatus;
            errorStatus[QStringLiteral("type")] = QStringLiteral("INVALID_REQUEST");
            errorStatus[QStringLiteral("requestId")] = requestId;
            errorStatus[QStringLiteral("reason")] = QStringLiteral("INVALID_PARAMS");

            QByteArray payload = QJsonDocument(errorStatus).toJson(QJsonDocument::Compact);
            CastMessage reply = makeJsonMsg(
                m_transportId,
                QString::fromStdString(msg.source_id()),
                QStringLiteral("urn:x-cast:com.google.cast.media"),
                payload);
            sendMessage(reply);
            return;
        }

        qDebug("CastSession: LOAD contentId='%s'", qPrintable(contentId));

        // Initiate URI pipeline (same pattern as DLNA, per D-10)
        if (m_pipeline) {
            // initUriPipeline initializes the pipeline structure;
            // setUri configures the source URL; playUri starts playback.
            if (m_pipeline->initUriPipeline(nullptr)) {
                m_pipeline->setUri(contentId.toStdString());
                m_pipeline->playUri();
                qDebug("CastSession: URI pipeline started for '%s'", qPrintable(contentId));
            } else {
                qWarning("CastSession: initUriPipeline failed for '%s'", qPrintable(contentId));
            }
        } else {
            qWarning("CastSession: no MediaPipeline set — cannot play '%s'",
                     qPrintable(contentId));
        }

        // Reply with MEDIA_STATUS: BUFFERING state
        int mediaSessionId = static_cast<int>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);

        QJsonObject mediaStatus;
        mediaStatus[QStringLiteral("mediaSessionId")] = mediaSessionId;
        mediaStatus[QStringLiteral("playerState")] = QStringLiteral("BUFFERING");
        mediaStatus[QStringLiteral("media")] = mediaObj;

        QJsonObject statusReply;
        statusReply[QStringLiteral("type")] = QStringLiteral("MEDIA_STATUS");
        statusReply[QStringLiteral("requestId")] = requestId;
        statusReply[QStringLiteral("status")] = QJsonArray{mediaStatus};

        QByteArray payload = QJsonDocument(statusReply).toJson(QJsonDocument::Compact);
        CastMessage reply = makeJsonMsg(
            m_transportId,
            QString::fromStdString(msg.source_id()),
            QStringLiteral("urn:x-cast:com.google.cast.media"),
            payload);
        sendMessage(reply);
    }
}

// ── Cast Streaming (OFFER / ANSWER) ───────────────────────────────────────────

// Whether GStreamer has a decoder for a Cast codec on this machine.
static bool gstCanDecode(const std::string& codec) {
    auto has = [](const char* f) {
        return gst_registry_check_feature_version(gst_registry_get(), f, 1, 0, 0);
    };
    if (codec == "vp8")  return has("vp8dec") || has("avdec_vp8");
    if (codec == "vp9")  return has("vp9dec") || has("avdec_vp9");
    if (codec == "h264") return has("avdec_h264") || has("vtdec") || has("vaapih264dec") ||
                                has("d3d11h264dec");
    if (codec == "opus") return has("opusdec");
    return false;
}

void CastSession::onWebrtc(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    const QJsonObject json = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8())).object();
    const QString type = json.value(QStringLiteral("type")).toString();
    const int seqNum   = json.value(QStringLiteral("seqNum")).toInt(-1);
    const QString src  = QString::fromStdString(msg.destination_id());
    const QString dst  = QString::fromStdString(msg.source_id());

    auto reply = [&](const QJsonObject& body) {
        sendMessage(makeJsonMsg(src, dst, QString::fromLatin1(kNsWebrtc),
                                QJsonDocument(body).toJson(QJsonDocument::Compact)));
    };

    if (type == QStringLiteral("GET_CAPABILITIES")) {
        // We don't implement remoting; Chrome treats this as "mirroring only".
        reply(cast::buildErrorReply(QStringLiteral("CAPABILITIES_RESPONSE"), seqNum, -1,
                                    QStringLiteral("Remoting is not supported")));
        return;
    }
    if (type != QStringLiteral("OFFER")) {
        qDebug("CastSession: ignoring webrtc message '%s'", qPrintable(type));
        return;
    }

    const auto offer = cast::parseOffer(json);
    if (!offer) {
        qWarning("CastSession: malformed OFFER");
        return;
    }
    const cast::CastSelection selection = cast::selectStreams(*offer, gstCanDecode);
    qDebug("CastSession: OFFER seqNum=%d with %zu stream(s); selected video=%s audio=%s",
           offer->seqNum, offer->streams.size(),
           selection.video ? selection.video->codec.c_str() : "none",
           selection.audio ? selection.audio->codec.c_str() : "none");
    if (selection.empty() || !m_pipeline) {
        reply(cast::buildErrorReply(QStringLiteral("ANSWER"), offer->seqNum, -1,
                                    QStringLiteral("No supported streams")));
        return;
    }

    // A new OFFER (e.g. resolution change) replaces the current stream.
    stopStreaming();

    std::vector<cast::CastStream> streams;
    if (selection.audio) streams.push_back(*selection.audio);
    if (selection.video) streams.push_back(*selection.video);

    m_streaming = std::make_unique<cast::CastStreamingReceiver>();
    if (m_socket) m_streaming->setExpectedSender(m_socket->peerAddress());
    MediaPipeline* pipeline = m_pipeline;
    m_streaming->setFrameCallback(
        [pipeline](cast::CastStream::Kind kind, const uint8_t* data, size_t size, bool key) {
            if (kind == cast::CastStream::Kind::Video) pipeline->pushCastVideoFrame(data, size, key);
            else                                       pipeline->pushCastAudioFrame(data, size);
        });

    if (!m_streaming->start(streams) ||
        !m_pipeline->initCastPipeline(selection.video ? selection.video->gstCaps() : std::string(),
                                      selection.audio ? selection.audio->gstCaps() : std::string())) {
        qWarning("CastSession: could not start Cast Streaming receiver/pipeline");
        stopStreaming();
        reply(cast::buildErrorReply(QStringLiteral("ANSWER"), offer->seqNum, -1,
                                    QStringLiteral("Receiver failed to start")));
        return;
    }

    reply(cast::buildAnswer(offer->seqNum, m_streaming->localPort(), selection));
    qDebug("CastSession: sent ANSWER (udpPort=%u)", m_streaming->localPort());
}

void CastSession::stopStreaming() {
    if (!m_streaming) return;
    m_streaming.reset();
    if (m_pipeline) m_pipeline->stopCast();
}

// ── Session teardown ──────────────────────────────────────────────────────────

void CastSession::onDisconnected() {
    qDebug("CastSession: socket disconnected");
    stopStreaming();
    // Only clear connected state if this session was the one that set it.
    // Probe sessions (auth challenge only, no CONNECT to transportId) must not
    // wipe another protocol's active connection (e.g. AirPlay).
    if (m_didConnect && m_connectionBridge) {
        QMetaObject::invokeMethod(m_connectionBridge, [this]() {
            m_connectionBridge->setConnected(false);
        }, Qt::QueuedConnection);
    }
    emit finished();
}

// ── Message serialization helpers ────────────────────────────────────────────

void CastSession::sendMessage(const CastMessage& msg) {
    std::string serialized;
    if (!msg.SerializeToString(&serialized)) {
        qWarning("CastSession: failed to serialize CastMessage");
        return;
    }

    // Prepend 4-byte big-endian length prefix
    uint32_t len = static_cast<uint32_t>(serialized.size());
    uint32_t lenBe = qToBigEndian<uint32_t>(len);

    QByteArray frame;
    frame.reserve(4 + static_cast<int>(len));
    frame.append(reinterpret_cast<const char*>(&lenBe), 4);
    frame.append(serialized.c_str(), static_cast<int>(len));

    m_socket->write(frame);
}

CastMessage CastSession::makeJsonMsg(const QString& src,
                                      const QString& dst,
                                      const QString& ns,
                                      const QByteArray& jsonPayload) {
    CastMessage msg;
    msg.set_protocol_version(CastMessage::CASTV2_1_0);
    msg.set_source_id(src.toStdString());
    msg.set_destination_id(dst.toStdString());
    msg.set_namespace_(ns.toStdString());
    msg.set_payload_type(CastMessage::STRING);
    msg.set_payload_utf8(jsonPayload.toStdString());
    return msg;
}

QByteArray CastSession::buildReceiverStatus(int requestId) const {
    QJsonObject status;
    status[QStringLiteral("type")] = QStringLiteral("RECEIVER_STATUS");
    status[QStringLiteral("requestId")] = requestId;

    QJsonObject statusInner;
    statusInner[QStringLiteral("isActiveInput")] = true;
    statusInner[QStringLiteral("isStandBy")] = false;

    QJsonObject volume;
    volume[QStringLiteral("level")] = 1.0;
    volume[QStringLiteral("muted")] = false;
    statusInner[QStringLiteral("volume")] = volume;

    QJsonArray applications;
    if (m_appLaunched && !m_launchedAppId.isEmpty()) {
        QJsonObject app;
        app[QStringLiteral("appId")] = m_launchedAppId;
        app[QStringLiteral("displayName")] =
            (m_launchedAppId == QLatin1StringView(kAppIdDefaultMedia))
                ? QStringLiteral("Default Media Receiver")
                : QStringLiteral("Chrome Mirroring");
        app[QStringLiteral("transportId")] = m_transportId;
        app[QStringLiteral("sessionId")] = m_transportId;
        app[QStringLiteral("isIdleScreen")] = false;

        // Namespaces this app supports
        QJsonArray namespaces;
        QJsonObject nsWebrtc;
        nsWebrtc[QStringLiteral("name")] = QStringLiteral("urn:x-cast:com.google.cast.webrtc");
        namespaces.append(nsWebrtc);
        QJsonObject nsMedia;
        nsMedia[QStringLiteral("name")] = QStringLiteral("urn:x-cast:com.google.cast.media");
        namespaces.append(nsMedia);
        QJsonObject nsConn;
        nsConn[QStringLiteral("name")] = QStringLiteral("urn:x-cast:com.google.cast.tp.connection");
        namespaces.append(nsConn);

        app[QStringLiteral("namespaces")] = namespaces;
        applications.append(app);
    }
    statusInner[QStringLiteral("applications")] = applications;
    status[QStringLiteral("status")] = statusInner;

    return QJsonDocument(status).toJson(QJsonDocument::Compact);
}

} // namespace airshow
