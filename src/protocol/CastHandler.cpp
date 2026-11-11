#include "protocol/CastHandler.h"
#include "protocol/CastCredentials.h"
#include "cast/CastReceiver.h"
#include "pipeline/MediaPipeline.h"
#include "security/SecurityManager.h"
#include "ui/ConnectionBridge.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>

#include <gst/gst.h>

namespace airshow {

CastHandler::CastHandler(ConnectionBridge* connectionBridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(connectionBridge)
{}

CastHandler::~CastHandler() {
    stop();
}

bool CastHandler::start() {
    if (m_receiver && m_receiver->isRunning()) return true;

    const auto& creds = castCredentials();
    if (!creds) return false;

    m_receiver = std::make_unique<cast::CastReceiver>(this, *creds);
    if (!m_receiver->start(8009)) {
        m_receiver.reset();
        return false;
    }
    qDebug("CastHandler: listening on port 8009 (TLS)");
    return true;
}

void CastHandler::stop() {
    if (!m_receiver) return;
    m_receiver.reset();  // stops the server and ends all sessions
    qDebug("CastHandler: stopped");
}

bool CastHandler::isRunning() const {
    return m_receiver && m_receiver->isRunning();
}

void CastHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void CastHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
}

// ── Admission ─────────────────────────────────────────────────────────────────

bool CastHandler::acceptPeer(const QHostAddress& peer) {
    // SEC-03: only RFC1918 / link-local senders.
    return SecurityManager::isLocalNetwork(peer);
}

void CastHandler::requestApproval(const QString& senderName, const QHostAddress& peer,
                                  std::function<void(bool)> done) {
    if (!m_securityManager) {
        done(true);
        return;
    }
    m_securityManager->checkConnectionAsync(senderName, QStringLiteral("Cast"),
                                            peer.toString(), std::move(done));
}

// ── Session state → HUD ───────────────────────────────────────────────────────

void CastHandler::senderConnected(const QString& senderName) {
    if (!m_connectionBridge) return;
    QMetaObject::invokeMethod(m_connectionBridge, [bridge = QPointer(m_connectionBridge), senderName]() {
        if (bridge) bridge->setConnected(true, senderName, QStringLiteral("Cast"));
    }, Qt::QueuedConnection);
}

void CastHandler::senderDisconnected() {
    if (!m_connectionBridge) return;
    QMetaObject::invokeMethod(m_connectionBridge, [bridge = QPointer(m_connectionBridge)]() {
        if (bridge) bridge->setConnected(false);
    }, Qt::QueuedConnection);
}

// ── Media → MediaPipeline ─────────────────────────────────────────────────────

bool CastHandler::canDecode(const std::string& codec) {
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

bool CastHandler::playUrl(const QString& url) {
    // Same URI pipeline as DLNA.
    if (!m_pipeline || !m_pipeline->initUriPipeline(nullptr)) return false;
    m_pipeline->setUri(url.toStdString());
    m_pipeline->playUri();
    qDebug("CastHandler: URI pipeline started for '%s'", qPrintable(url));
    return true;
}

bool CastHandler::startStream(const std::string& videoCaps, const std::string& audioCaps) {
    return m_pipeline && m_pipeline->initCastPipeline(videoCaps, audioCaps);
}

void CastHandler::pushVideoFrame(const uint8_t* data, size_t size, bool keyFrame) {
    if (m_pipeline) m_pipeline->pushCastVideoFrame(data, size, keyFrame);
}

void CastHandler::pushAudioFrame(const uint8_t* data, size_t size) {
    if (m_pipeline) m_pipeline->pushCastAudioFrame(data, size);
}

void CastHandler::stopStream() {
    if (m_pipeline) m_pipeline->stopCast();
}

} // namespace airshow
