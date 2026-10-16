#pragma once
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Google Cast Streaming session negotiation (OFFER / ANSWER on the
// urn:x-cast:com.google.cast.webrtc namespace). Field names and rules follow
// openscreen's cast/streaming/public/{offer,answer}_messages.cc.

namespace airshow::cast {

// One media stream from an OFFER's "supportedStreams".
struct CastStream {
    enum class Kind { Audio, Video };

    Kind        kind           = Kind::Video;
    int         index          = -1;   // echoed back in ANSWER "sendIndexes"
    std::string codec;                 // lower-case "codecName": "vp8", "opus", ...
    uint32_t    senderSsrc     = 0;
    int         rtpPayloadType = 0;    // Chrome sends 96 (video) / 127 (audio) for every codec
    int         rtpTimebase    = 0;    // denominator of "timeBase" ("1/90000" -> 90000)
    int         channels       = 0;
    int         targetDelayMs  = 400;
    std::array<uint8_t, 16> aesKey{};
    std::array<uint8_t, 16> aesIvMask{};

    // Our SSRC for this stream's RTCP: sender SSRC + 1, as openscreen's receiver uses.
    uint32_t receiverSsrc() const { return senderSsrc + 1; }

    // appsrc caps for MediaPipeline::initCastPipeline(), or "" if unsupported.
    std::string gstCaps() const;
};

struct CastOffer {
    int seqNum = -1;
    std::vector<CastStream> streams;  // every stream that parsed and validated
};

// Parse a whole OFFER message: {"type":"OFFER","seqNum":N,"offer":{"supportedStreams":[...]}}.
// Streams with missing/invalid mandatory fields are skipped. Returns nullopt if
// the message isn't an OFFER or has no seqNum.
std::optional<CastOffer> parseOffer(const QJsonObject& message);

// At most one audio and one video stream.
struct CastSelection {
    std::optional<CastStream> audio;
    std::optional<CastStream> video;
    bool empty() const { return !audio && !video; }
};

// Pick the streams to receive. Video codec preference: vp8, vp9, h264 — VP8 is
// always offered by Chrome and its frames are plain libvpx output; H.264 is only
// offered with a hardware encoder. canDecode() reports whether a decoder exists.
CastSelection selectStreams(const CastOffer& offer,
                            const std::function<bool(const std::string& codec)>& canDecode);

// {"type":"ANSWER","seqNum":N,"result":"ok","answer":{"udpPort":P,"sendIndexes":[..],"ssrcs":[..]}}
// Audio first, then video. constraints/display are omitted (all optional).
QJsonObject buildAnswer(int seqNum, uint16_t udpPort, const CastSelection& selection);

// {"type":<type>,"seqNum":N,"result":"error","error":{"code":C,"description":D}}
QJsonObject buildErrorReply(const QString& type, int seqNum, int code, const QString& description);

} // namespace airshow::cast
