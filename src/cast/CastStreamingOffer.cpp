#include "cast/CastStreamingOffer.h"

#include <QJsonArray>
#include <QJsonValue>

namespace airshow::cast {

namespace {

// 32 hex chars -> 16 bytes (plain byte-order hex decode, as openscreen does).
bool parseHex128(const QString& hex, std::array<uint8_t, 16>& out) {
    if (hex.size() != 32) return false;
    for (int i = 0; i < 16; ++i) {
        bool ok = false;
        const uint v = QStringView(hex).mid(i * 2, 2).toUInt(&ok, 16);
        if (!ok) return false;
        out[static_cast<size_t>(i)] = static_cast<uint8_t>(v);
    }
    return true;
}

// "1/48000" -> 48000. openscreen requires numerator 1 and N in [16000, 90000].
int parseTimebase(const QString& s) {
    const auto parts = s.split(QLatin1Char('/'));
    if (parts.size() != 2 || parts[0].trimmed() != QLatin1String("1")) return 0;
    bool ok = false;
    const int n = parts[1].trimmed().toInt(&ok);
    return (ok && n >= 16000 && n <= 90000) ? n : 0;
}

std::optional<CastStream> parseStream(const QJsonObject& o) {
    CastStream s;
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("audio_source"))      s.kind = CastStream::Kind::Audio;
    else if (type == QLatin1String("video_source")) s.kind = CastStream::Kind::Video;
    else return std::nullopt;

    if (!o.value(QStringLiteral("index")).isDouble() ||
        !o.value(QStringLiteral("ssrc")).isDouble() ||
        !o.value(QStringLiteral("rtpPayloadType")).isDouble()) {
        return std::nullopt;
    }
    s.index          = o.value(QStringLiteral("index")).toInt();
    s.senderSsrc     = static_cast<uint32_t>(o.value(QStringLiteral("ssrc")).toDouble());
    s.rtpPayloadType = o.value(QStringLiteral("rtpPayloadType")).toInt();
    s.codec          = o.value(QStringLiteral("codecName")).toString().toLower().toStdString();
    s.rtpTimebase    = parseTimebase(o.value(QStringLiteral("timeBase")).toString());
    s.channels       = o.value(QStringLiteral("channels"))
                           .toInt(s.kind == CastStream::Kind::Audio ? 2 : 1);
    const int delay  = o.value(QStringLiteral("targetDelay")).toInt(400);
    s.targetDelayMs  = (delay > 0 && delay <= 5000) ? delay : 400;

    if (s.index < 0 || s.codec.empty() || s.rtpTimebase == 0 ||
        !parseHex128(o.value(QStringLiteral("aesKey")).toString(), s.aesKey) ||
        !parseHex128(o.value(QStringLiteral("aesIvMask")).toString(), s.aesIvMask)) {
        return std::nullopt;
    }
    return s;
}

} // namespace

std::string CastStream::gstCaps() const {
    if (kind == Kind::Video) {
        if (codec == "vp8")  return "video/x-vp8";
        if (codec == "vp9")  return "video/x-vp9";
        // Frames carry Annex-B start codes with in-band SPS/PPS (what openscreen's
        // own receiver assumes when it feeds them to FFmpeg without extradata).
        if (codec == "h264") return "video/x-h264,stream-format=byte-stream,alignment=au";
        return {};
    }
    // One raw Opus packet per Cast frame; mapping family 0 needs no OpusHead.
    if (codec == "opus" && channels >= 1 && channels <= 2) {
        return "audio/x-opus,channel-mapping-family=0,channels=" + std::to_string(channels) +
               ",rate=" + std::to_string(rtpTimebase);
    }
    return {};
}

std::optional<CastOffer> parseOffer(const QJsonObject& message) {
    if (message.value(QStringLiteral("type")).toString() != QLatin1String("OFFER") ||
        !message.value(QStringLiteral("seqNum")).isDouble()) {
        return std::nullopt;
    }
    CastOffer offer;
    offer.seqNum = message.value(QStringLiteral("seqNum")).toInt();
    const QJsonArray streams = message.value(QStringLiteral("offer")).toObject()
                                   .value(QStringLiteral("supportedStreams")).toArray();
    for (const QJsonValue& v : streams) {
        if (auto s = parseStream(v.toObject())) offer.streams.push_back(*s);
    }
    return offer;
}

CastSelection selectStreams(const CastOffer& offer,
                            const std::function<bool(const std::string&)>& canDecode) {
    CastSelection sel;
    auto usable = [&](const CastStream& s) {
        return !s.gstCaps().empty() && (!canDecode || canDecode(s.codec));
    };

    for (const char* codec : {"vp8", "vp9", "h264"}) {
        for (const CastStream& s : offer.streams) {
            if (s.kind == CastStream::Kind::Video && s.codec == codec && usable(s)) {
                sel.video = s;
                break;
            }
        }
        if (sel.video) break;
    }
    for (const CastStream& s : offer.streams) {
        if (s.kind == CastStream::Kind::Audio && usable(s)) {
            sel.audio = s;
            break;
        }
    }
    return sel;
}

QJsonObject buildAnswer(int seqNum, uint16_t udpPort, const CastSelection& selection) {
    QJsonArray indexes, ssrcs;
    for (const auto* s : {&selection.audio, &selection.video}) {
        if (!*s) continue;
        indexes.append((*s)->index);
        ssrcs.append(static_cast<qint64>((*s)->receiverSsrc()));
    }
    QJsonObject answer;
    answer[QStringLiteral("udpPort")]     = static_cast<int>(udpPort);
    answer[QStringLiteral("sendIndexes")] = indexes;
    answer[QStringLiteral("ssrcs")]       = ssrcs;

    QJsonObject reply;
    reply[QStringLiteral("type")]   = QStringLiteral("ANSWER");
    reply[QStringLiteral("seqNum")] = seqNum;
    reply[QStringLiteral("result")] = QStringLiteral("ok");
    reply[QStringLiteral("answer")] = answer;
    return reply;
}

QJsonObject buildErrorReply(const QString& type, int seqNum, int code, const QString& description) {
    QJsonObject error;
    error[QStringLiteral("code")]        = code;
    error[QStringLiteral("description")] = description;

    QJsonObject reply;
    reply[QStringLiteral("type")]   = type;
    reply[QStringLiteral("seqNum")] = seqNum;
    reply[QStringLiteral("result")] = QStringLiteral("error");
    reply[QStringLiteral("error")]  = error;
    return reply;
}

} // namespace airshow::cast
