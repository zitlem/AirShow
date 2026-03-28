#include "ui/ConnectionBridge.h"

namespace myairshow {

ConnectionBridge::ConnectionBridge(QObject* parent)
    : QObject(parent) {}

void ConnectionBridge::setConnected(bool connected,
                                    const QString& deviceName,
                                    const QString& protocol)
{
    // When disconnecting, always clear device info regardless of what was passed.
    // Invariant: disconnected state has no device name or protocol (D-05).
    if (!connected) {
        m_connected  = false;
        m_deviceName = {};
        m_protocol   = {};
    } else {
        m_connected  = true;
        m_deviceName = deviceName;
        m_protocol   = protocol;
    }

    emit connectedChanged(m_connected);
    emit deviceNameChanged(m_deviceName);
    emit protocolChanged(m_protocol);
}

} // namespace myairshow
