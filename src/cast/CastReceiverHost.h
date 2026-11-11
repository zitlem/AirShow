// Copyright 2026 zitlem
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <QHostAddress>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace airshow::cast {

// What the Cast receiver needs from the application embedding it: admission
// policy, connection notifications, and somewhere to play media. Everything is
// called on the thread that owns the CastReceiver (the Qt event loop thread).
// The host must outlive the receiver.
class CastReceiverHost {
public:
    virtual ~CastReceiverHost() = default;

    // ── Admission ────────────────────────────────────────────────────────────
    // Called for each TCP connection before any CASTV2 traffic. Return false
    // to drop it.
    virtual bool acceptPeer(const QHostAddress& /*peer*/) { return true; }

    // Called once per session, on the first message that actually starts a cast
    // (discovery probes and status polling never ask). Call done(true/false),
    // now or later; the session may already be gone by then, which is safe.
    virtual void requestApproval(const QString& /*senderName*/, const QHostAddress& /*peer*/,
                                 std::function<void(bool approved)> done) {
        done(true);
    }

    // ── Session state ────────────────────────────────────────────────────────
    // A sender connected to a launched app (a cast is starting) / that cast ended.
    virtual void senderConnected(const QString& /*senderName*/) {}
    virtual void senderDisconnected() {}

    // ── Media ────────────────────────────────────────────────────────────────
    // Whether a Cast Streaming codec ("vp8", "vp9", "h264", "opus") can be decoded.
    virtual bool canDecode(const std::string& codec) = 0;

    // Default Media Receiver LOAD: play a URL. Return false if playback could not start.
    virtual bool playUrl(const QString& url) = 0;

    // Cast Streaming (mirroring). startStream() is given GStreamer-style caps for
    // the selected streams (empty when that kind is absent); then decrypted,
    // reassembled frames arrive via push*Frame(); stopStream() ends it.
    virtual bool startStream(const std::string& videoCaps, const std::string& audioCaps) = 0;
    virtual void pushVideoFrame(const uint8_t* data, size_t size, bool keyFrame) = 0;
    virtual void pushAudioFrame(const uint8_t* data, size_t size) = 0;
    virtual void stopStream() = 0;
};

} // namespace airshow::cast
