#pragma once
#include "protocol/ProtocolHandler.h"
#include "cast/CastReceiverHost.h"

#include <QObject>
#include <memory>
#include <string>

namespace airshow {

namespace cast { class CastReceiver; }
class ConnectionBridge;
class SecurityManager;

// AirShow's Google Cast protocol handler: runs a cast::CastReceiver on port 8009
// and connects it to the rest of AirShow — connection approval and the
// local-network filter (SecurityManager), the HUD (ConnectionBridge), and
// playback (MediaPipeline).
class CastHandler : public QObject, public ProtocolHandler, public cast::CastReceiverHost {
    Q_OBJECT
public:
    explicit CastHandler(ConnectionBridge* connectionBridge, QObject* parent = nullptr);
    ~CastHandler() override;

    // ProtocolHandler
    bool        start() override;
    void        stop() override;
    std::string name() const override { return "cast"; }
    bool        isRunning() const override;
    void        setMediaPipeline(MediaPipeline* pipeline) override;

    // Optional — without it every connection is admitted. Call before start().
    void        setSecurityManager(SecurityManager* sm);

    // cast::CastReceiverHost
    bool acceptPeer(const QHostAddress& peer) override;
    void requestApproval(const QString& senderName, const QHostAddress& peer,
                         std::function<void(bool)> done) override;
    void senderConnected(const QString& senderName) override;
    void senderDisconnected() override;
    bool canDecode(const std::string& codec) override;
    bool playUrl(const QString& url) override;
    bool startStream(const std::string& videoCaps, const std::string& audioCaps) override;
    void pushVideoFrame(const uint8_t* data, size_t size, bool keyFrame) override;
    void pushAudioFrame(const uint8_t* data, size_t size) override;
    void stopStream() override;

private:
    std::unique_ptr<cast::CastReceiver> m_receiver;
    ConnectionBridge*                   m_connectionBridge = nullptr;
    MediaPipeline*                      m_pipeline         = nullptr;
    SecurityManager*                    m_securityManager  = nullptr;
};

} // namespace airshow
