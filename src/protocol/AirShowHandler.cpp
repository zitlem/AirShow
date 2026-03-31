// AirShowHandler.cpp — stub for TDD RED phase (Task 1)
// Full implementation in Task 2 (GREEN phase).
#include "protocol/AirShowHandler.h"

namespace airshow {

AirShowHandler::AirShowHandler(ConnectionBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_connectionBridge(bridge)
{}

AirShowHandler::~AirShowHandler() {
    stop();
}

bool AirShowHandler::start() {
    return false;  // RED: not implemented yet
}

void AirShowHandler::stop() {}

void AirShowHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

void AirShowHandler::setSecurityManager(SecurityManager* sm) {
    m_security = sm;
}

bool AirShowHandler::parseFrameHeader(const QByteArray& /*data*/, FrameHeader& /*out*/) {
    return false;  // RED: not implemented yet
}

void AirShowHandler::onNewConnection() {}
void AirShowHandler::onReadyRead() {}
void AirShowHandler::onDisconnected() {}
void AirShowHandler::handleHandshake() {}
void AirShowHandler::handleStreamingData() {}
void AirShowHandler::processFrame(const QByteArray& /*frameData*/) {}
void AirShowHandler::disconnectClient() {}

} // namespace airshow
