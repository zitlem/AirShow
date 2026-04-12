#include "ui/ConnectionBridge.h"
#include <QDebug>

namespace airshow {

ConnectionBridge::ConnectionBridge(QObject* parent)
    : QObject(parent) {}

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

void ConnectionBridge::setConnected(bool connected,
                                    const QString& deviceName,
                                    const QString& protocol)
{
    qDebug("ConnectionBridge::setConnected(%s, '%s', '%s')",
           connected ? "true" : "false",
           qPrintable(deviceName), qPrintable(protocol));
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

// ---------------------------------------------------------------------------
// Approval dialog state (Phase 7 / D-13)
// ---------------------------------------------------------------------------

void ConnectionBridge::showApprovalRequest(const QString& requestId,
                                           const QString& deviceName,
                                           const QString& protocol)
{
    m_pendingRequestId  = requestId;
    m_pendingDeviceName = deviceName;
    m_pendingProtocol   = protocol;
    m_approvalPending   = true;

    emit pendingRequestIdChanged(m_pendingRequestId);
    emit pendingDeviceNameChanged(m_pendingDeviceName);
    emit pendingProtocolChanged(m_pendingProtocol);
    emit approvalPendingChanged(true);
}

void ConnectionBridge::clearApprovalRequest()
{
    m_pendingRequestId  = {};
    m_pendingDeviceName = {};
    m_pendingProtocol   = {};
    m_approvalPending   = false;

    emit pendingRequestIdChanged(QString());
    emit pendingDeviceNameChanged(QString());
    emit pendingProtocolChanged(QString());
    emit approvalPendingChanged(false);
}

} // namespace airshow
