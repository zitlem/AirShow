#include "protocol/CastSession.h"
#include "ui/ConnectionBridge.h"
#include "pipeline/MediaPipeline.h"
#include "cast/cast_auth_sigs.h"

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

// ── CastSession ───────────────────────────────────────────────────────────────

CastSession::CastSession(QSslSocket* socket,
                          ConnectionBridge* connectionBridge,
                          MediaPipeline* pipeline,
                          QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_connectionBridge(connectionBridge)
    , m_pipeline(pipeline)
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

void CastSession::dispatchMessage(const CastMessage& msg) {
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

    // Look up precomputed signature for current 48h window
    uint64_t nowSecs = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    size_t idx = (nowSecs / 172800) % cast::kCastAuthSignatureCount;
    const uint8_t* sig = &cast::kCastAuthSignatures[idx][0];

    // Build AuthResponse
    AuthResponse authResponse;
    authResponse.set_signature(
        reinterpret_cast<const char*>(sig),
        cast::kCastAuthSignatureSize);
    authResponse.set_client_auth_certificate(
        reinterpret_cast<const char*>(cast::kCastAuthPeerCert),
        cast::kCastAuthPeerCertSize);
    authResponse.set_signature_algorithm(
        extensions::api::cast_channel::RSASSA_PKCS1v15);
    authResponse.set_hash_algorithm(
        extensions::api::cast_channel::SHA256);

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

        // If connecting to our transportId (not receiver-0), announce connection
        if (msg.destination_id() == m_transportId.toStdString()) {
            qDebug("CastSession: CONNECT to transportId from '%s'",
                   qPrintable(m_senderName));
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
        if (m_connectionBridge) {
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

// ── Static helper: Cast OFFER JSON -> SDP ────────────────────────────────────
//
// Translates a Cast WebRTC OFFER JSON object (from the urn:x-cast:com.google.cast.webrtc
// namespace) into a standard SDP string suitable for GStreamer's webrtcbin.
//
// Cast OFFER JSON structure (per RESEARCH.md Pattern 6):
// {
//   "type": "OFFER",
//   "seqNum": 1,
//   "offer": {
//     "supportedStreams": [
//       { "type": "video_source", "codecName": "vp8", "rtpPayloadType": 96,
//         "ssrc": 12345678, "aesKey": "...", "aesIvMask": "..." },
//       { "type": "audio_source", "codecName": "opus", "rtpPayloadType": 97,
//         "ssrc": 87654321, "sampleRate": 48000, "channels": 2 }
//     ]
//   }
// }
//
// Returns an SDP string. Returns empty string on failure.
std::string CastSession::buildSdpFromOffer(const QJsonObject& offerJson)
{
    QJsonObject innerOffer = offerJson.value(QStringLiteral("offer")).toObject();
    if (innerOffer.isEmpty()) {
        // Also try the object directly if it's the inner offer
        innerOffer = offerJson;
    }

    QJsonArray streams = innerOffer.value(QStringLiteral("supportedStreams")).toArray();
    if (streams.isEmpty()) {
        qWarning("CastSession::buildSdpFromOffer — no supportedStreams in offer");
        return {};
    }

    // Extract video and audio stream parameters
    struct StreamInfo {
        int    payloadType = 96;
        uint32_t ssrc     = 0;
        QString  codec;
        int    sampleRate = 48000;
        int    channels   = 2;
    };

    StreamInfo videoStream, audioStream;
    bool hasVideo = false, hasAudio = false;

    for (const QJsonValue& sv : streams) {
        QJsonObject stream = sv.toObject();
        QString streamType = stream.value(QStringLiteral("type")).toString();
        QString codec      = stream.value(QStringLiteral("codecName")).toString().toLower();
        int pt             = stream.value(QStringLiteral("rtpPayloadType")).toInt(96);
        uint32_t ssrc      = static_cast<uint32_t>(stream.value(QStringLiteral("ssrc")).toDouble(0));

        if (streamType == QStringLiteral("video_source")) {
            videoStream.codec       = codec;
            videoStream.payloadType = pt;
            videoStream.ssrc        = ssrc;
            hasVideo = true;
        } else if (streamType == QStringLiteral("audio_source")) {
            audioStream.codec       = codec;
            audioStream.payloadType = pt;
            audioStream.ssrc        = ssrc;
            audioStream.sampleRate  = stream.value(QStringLiteral("sampleRate")).toInt(48000);
            audioStream.channels    = stream.value(QStringLiteral("channels")).toInt(2);
            hasAudio = true;
        }
    }

    if (!hasVideo && !hasAudio) {
        qWarning("CastSession::buildSdpFromOffer — no video or audio stream found");
        return {};
    }

    // Build SDP string following RFC 4566 / RFC 3264.
    // The 'a=recvonly' direction indicates this receiver accepts media from the sender.
    // ICE credentials and DTLS fingerprint are placeholders here — webrtcbin generates
    // the real values via set-remote-description / create-answer exchange.
    std::string sdp;
    sdp += "v=0\r\n";
    sdp += "o=- 1234567890 1 IN IP4 0.0.0.0\r\n";
    sdp += "s=Cast\r\n";
    sdp += "t=0 0\r\n";

    // BUNDLE group (all m-lines share one DTLS transport)
    if (hasVideo && hasAudio) {
        sdp += "a=group:BUNDLE 0 1\r\n";
    } else if (hasVideo) {
        sdp += "a=group:BUNDLE 0\r\n";
    } else {
        sdp += "a=group:BUNDLE 1\r\n";
    }
    sdp += "a=msid-semantic:WMS *\r\n";

    // Video m-line
    if (hasVideo) {
        std::string vcodec = "VP8";
        if (videoStream.codec.toLower() == QStringLiteral("vp9")) vcodec = "VP9";

        sdp += "m=video 9 UDP/TLS/RTP/SAVPF " + std::to_string(videoStream.payloadType) + "\r\n";
        sdp += "c=IN IP4 0.0.0.0\r\n";
        sdp += "a=rtcp:9 IN IP4 0.0.0.0\r\n";
        sdp += "a=mid:0\r\n";
        sdp += "a=recvonly\r\n";
        sdp += "a=rtcp-mux\r\n";
        sdp += "a=rtpmap:" + std::to_string(videoStream.payloadType) + " " + vcodec + "/90000\r\n";
        if (videoStream.ssrc != 0) {
            sdp += "a=ssrc:" + std::to_string(videoStream.ssrc) + " cname:cast\r\n";
        }
        sdp += "a=setup:actpass\r\n";
    }

    // Audio m-line
    if (hasAudio) {
        std::string acodec = "OPUS";
        std::string rateChannels = std::to_string(audioStream.sampleRate) + "/" +
                                   std::to_string(audioStream.channels);

        sdp += "m=audio 9 UDP/TLS/RTP/SAVPF " + std::to_string(audioStream.payloadType) + "\r\n";
        sdp += "c=IN IP4 0.0.0.0\r\n";
        sdp += "a=rtcp:9 IN IP4 0.0.0.0\r\n";
        sdp += "a=mid:1\r\n";
        sdp += "a=recvonly\r\n";
        sdp += "a=rtcp-mux\r\n";
        sdp += "a=rtpmap:" + std::to_string(audioStream.payloadType) + " " + acodec + "/" + rateChannels + "\r\n";
        if (audioStream.ssrc != 0) {
            sdp += "a=ssrc:" + std::to_string(audioStream.ssrc) + " cname:cast\r\n";
        }
        sdp += "a=setup:actpass\r\n";
    }

    return sdp;
}

void CastSession::onWebrtc(const CastMessage& msg) {
    if (msg.payload_type() != CastMessage::STRING) return;

    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(msg.payload_utf8()));
    if (doc.isNull()) {
        qWarning("CastSession: onWebrtc — failed to parse JSON payload");
        return;
    }

    QJsonObject json = doc.object();
    QString type     = json.value(QStringLiteral("type")).toString();
    int seqNum       = json.value(QStringLiteral("seqNum")).toInt(0);

    if (type != QStringLiteral("OFFER")) {
        qDebug("CastSession: onWebrtc — ignoring message type '%s'", qPrintable(type));
        return;
    }

    qDebug("CastSession: onWebrtc — received OFFER (seqNum=%d)", seqNum);

    // Extract supportedStreams for AES-CTR key injection (Pitfall 3)
    QJsonObject innerOffer = json.value(QStringLiteral("offer")).toObject();
    QJsonArray streams     = innerOffer.value(QStringLiteral("supportedStreams")).toArray();

    for (const QJsonValue& sv : streams) {
        QJsonObject stream = sv.toObject();
        QString aesKey    = stream.value(QStringLiteral("aesKey")).toString();
        QString aesIvMask = stream.value(QStringLiteral("aesIvMask")).toString();
        uint32_t ssrc     = static_cast<uint32_t>(stream.value(QStringLiteral("ssrc")).toDouble(0));

        if (!aesKey.isEmpty() && ssrc != 0 && m_pipeline) {
            qDebug("CastSession: storing AES-CTR keys for SSRC %u", ssrc);
            m_pipeline->setCastDecryptionKeys(
                ssrc, aesKey.toStdString(), aesIvMask.toStdString());
        }
    }

    // Translate Cast OFFER JSON to standard SDP for webrtcbin (Pitfall 2)
    std::string sdpOffer = buildSdpFromOffer(json);
    if (sdpOffer.empty()) {
        qWarning("CastSession: onWebrtc — buildSdpFromOffer failed, cannot proceed");
        return;
    }

    qDebug("CastSession: onWebrtc — SDP offer built (%zu bytes)", sdpOffer.size());

    if (!m_pipeline) {
        qWarning("CastSession: onWebrtc — no MediaPipeline; cannot initiate WebRTC");
        return;
    }

    // Initialize WebRTC pipeline (uses stored m_qmlVideoItem, NO argument)
    if (!m_pipeline->initWebrtcPipeline()) {
        qWarning("CastSession: onWebrtc — initWebrtcPipeline() failed");
        return;
    }

    // Feed the SDP offer to webrtcbin and obtain the answer
    if (!m_pipeline->setRemoteOffer(sdpOffer)) {
        qWarning("CastSession: onWebrtc — setRemoteOffer() failed");
        return;
    }

    std::string answerSdp = m_pipeline->getLocalAnswer();
    if (answerSdp.empty()) {
        qWarning("CastSession: onWebrtc — getLocalAnswer() returned empty string");
        return;
    }

    // Parse the answer SDP to extract local SSRCs for the ANSWER JSON
    // For the Cast ANSWER, we report send indexes [0, 1] and our local SSRCs.
    // Since we are a pure receiver, we use the sender's SSRCs as references.
    // Extract video/audio SSRCs from the offer for the sendIndexes response.
    int videoPayloadType = 96, audioPayloadType = 97;
    uint32_t videoSsrc = 0, audioSsrc = 0;

    for (const QJsonValue& sv : streams) {
        QJsonObject stream    = sv.toObject();
        QString streamType    = stream.value(QStringLiteral("type")).toString();
        if (streamType == QStringLiteral("video_source")) {
            videoPayloadType = stream.value(QStringLiteral("rtpPayloadType")).toInt(96);
            videoSsrc        = static_cast<uint32_t>(stream.value(QStringLiteral("ssrc")).toDouble(0));
        } else if (streamType == QStringLiteral("audio_source")) {
            audioPayloadType = stream.value(QStringLiteral("rtpPayloadType")).toInt(97);
            audioSsrc        = static_cast<uint32_t>(stream.value(QStringLiteral("ssrc")).toDouble(0));
        }
    }
    Q_UNUSED(videoPayloadType)
    Q_UNUSED(audioPayloadType)

    // Build Cast ANSWER JSON (per RESEARCH.md Pattern 6)
    // udpPort is the local ICE port — webrtcbin will handle the actual connection.
    // For the JSON response, we use a nominal port value of 9 (standard SDP dummy port).
    QJsonObject answerBody;
    answerBody[QStringLiteral("udpPort")]     = 9;
    answerBody[QStringLiteral("sendIndexes")] = QJsonArray{0, 1};

    QJsonArray ssrcs;
    if (videoSsrc != 0) ssrcs.append(static_cast<qint64>(videoSsrc));
    if (audioSsrc != 0) ssrcs.append(static_cast<qint64>(audioSsrc));
    if (ssrcs.isEmpty()) {
        ssrcs.append(0);
        ssrcs.append(1);
    }
    answerBody[QStringLiteral("ssrcs")] = ssrcs;

    QJsonObject reply;
    reply[QStringLiteral("type")]   = QStringLiteral("ANSWER");
    reply[QStringLiteral("seqNum")] = seqNum;
    reply[QStringLiteral("answer")] = answerBody;
    reply[QStringLiteral("result")] = QStringLiteral("ok");

    QByteArray payload = QJsonDocument(reply).toJson(QJsonDocument::Compact);
    CastMessage replyMsg = makeJsonMsg(
        QString::fromStdString(msg.destination_id()),
        QString::fromStdString(msg.source_id()),
        QStringLiteral("urn:x-cast:com.google.cast.webrtc"),
        payload);
    sendMessage(replyMsg);

    qDebug("CastSession: onWebrtc — sent ANSWER (seqNum=%d)", seqNum);

    // Transition the WebRTC pipeline to PLAYING state.
    // MediaPipeline::play() sets the main appsrc pipeline to PLAYING.
    // For the webrtcbin pipeline, call play() which acts on m_pipeline.
    // However, the webrtcbin pipeline is separate from m_pipeline (the appsrc pipeline).
    // Use the public play() method which handles the webrtcbin pipeline indirectly.
    // The webrtcbin pipeline was put to PAUSED in initWebrtcPipeline() — now go to PLAYING.
    m_pipeline->play();

    qDebug("CastSession: onWebrtc — WebRTC pipeline transitioning to PLAYING");
}

// ── Session teardown ──────────────────────────────────────────────────────────

void CastSession::onDisconnected() {
    qDebug("CastSession: socket disconnected");
    if (m_connectionBridge) {
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
            (m_launchedAppId == QLatin1StringView(kAppIdChromeMirror))
                ? QStringLiteral("Chrome Mirroring")
                : QStringLiteral("Default Media Receiver");
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
