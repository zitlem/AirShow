#pragma once
#include <QObject>
#include <QString>

namespace airshow {

// QObject bridge that exposes connection state to QML via Q_PROPERTY.
// QML binds to connectionBridge.connected, connectionBridge.deviceName,
// connectionBridge.protocol. Phase 4 protocol handlers call setConnected()
// as the single mutation point.
//
// Starts in the idle (disconnected) state: connected=false, deviceName="", protocol="".
//
// Phase 7 (D-13): Extended with approval dialog state properties and slots.
// When SecurityManager emits requestApproval(), ConnectionBridge::showApprovalRequest()
// is connected to it so that QML can display the approval overlay by reading
// the approvalPending, pendingDeviceName, pendingProtocol, pendingRequestId properties.
class ConnectionBridge : public QObject {
    Q_OBJECT

    // --- Connection state ------------------------------------------------
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString protocol READ protocol NOTIFY protocolChanged)

    // --- Approval dialog state (Phase 7 / D-13) --------------------------
    // approvalPending: true while an approval request is waiting for user action
    Q_PROPERTY(bool approvalPending
               READ isApprovalPending
               NOTIFY approvalPendingChanged)

    Q_PROPERTY(QString pendingDeviceName
               READ pendingDeviceName
               NOTIFY pendingDeviceNameChanged)

    Q_PROPERTY(QString pendingProtocol
               READ pendingProtocol
               NOTIFY pendingProtocolChanged)

    // requestId is forwarded to SecurityManager::resolveApproval() by QML
    Q_PROPERTY(QString pendingRequestId
               READ pendingRequestId
               NOTIFY pendingRequestIdChanged)

public:
    explicit ConnectionBridge(QObject* parent = nullptr);

    // --- Connection state accessors -----------------------------------------------
    bool    isConnected() const { return m_connected; }
    QString deviceName()  const { return m_deviceName; }
    QString protocol()    const { return m_protocol; }

    // Single mutation point for all protocol handlers (Phase 4).
    // When disconnecting, pass connected=false and omit (or leave empty)
    // deviceName and protocol — the bridge will clear them automatically.
    void setConnected(bool connected,
                      const QString& deviceName = {},
                      const QString& protocol = {});

    // --- Approval dialog state accessors ------------------------------------------
    bool    isApprovalPending()  const { return m_approvalPending; }
    QString pendingDeviceName()  const { return m_pendingDeviceName; }
    QString pendingProtocol()    const { return m_pendingProtocol; }
    QString pendingRequestId()   const { return m_pendingRequestId; }

    // Called when SecurityManager emits requestApproval(). Sets pending properties
    // and emits approvalPendingChanged(true) so QML shows the approval dialog.
    void showApprovalRequest(const QString& requestId,
                             const QString& deviceName,
                             const QString& protocol);

    // Clears all pending approval properties and emits approvalPendingChanged(false).
    // Called after the user has approved or denied (QML triggers resolveApproval
    // on SecurityManager, then calls clearApprovalRequest via an invokable or slot).
    Q_INVOKABLE void clearApprovalRequest();

signals:
    // Connection state signals
    void connectedChanged(bool connected);
    void deviceNameChanged(const QString& deviceName);
    void protocolChanged(const QString& protocol);

    // Approval dialog signals
    void approvalPendingChanged(bool approvalPending);
    void pendingDeviceNameChanged(const QString& pendingDeviceName);
    void pendingProtocolChanged(const QString& pendingProtocol);
    void pendingRequestIdChanged(const QString& pendingRequestId);

private:
    // Connection state
    bool    m_connected  = false;
    QString m_deviceName;
    QString m_protocol;

    // Approval dialog state
    bool    m_approvalPending   = false;
    QString m_pendingDeviceName;
    QString m_pendingProtocol;
    QString m_pendingRequestId;
};

} // namespace airshow
