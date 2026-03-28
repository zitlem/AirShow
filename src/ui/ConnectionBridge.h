#pragma once
#include <QObject>
#include <QString>

namespace myairshow {

// QObject bridge that exposes connection state to QML via Q_PROPERTY.
// QML binds to connectionBridge.connected, connectionBridge.deviceName,
// connectionBridge.protocol. Phase 4 protocol handlers call setConnected()
// as the single mutation point.
//
// Starts in the idle (disconnected) state: connected=false, deviceName="", protocol="".
class ConnectionBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString protocol READ protocol NOTIFY protocolChanged)

public:
    explicit ConnectionBridge(QObject* parent = nullptr);

    bool isConnected() const { return m_connected; }
    QString deviceName() const { return m_deviceName; }
    QString protocol() const { return m_protocol; }

    // Single mutation point for all protocol handlers (Phase 4).
    // When disconnecting, pass connected=false and omit (or leave empty)
    // deviceName and protocol — the bridge will clear them automatically.
    void setConnected(bool connected,
                      const QString& deviceName = {},
                      const QString& protocol = {});

signals:
    void connectedChanged(bool connected);
    void deviceNameChanged(const QString& deviceName);
    void protocolChanged(const QString& protocol);

private:
    bool    m_connected  = false;
    QString m_deviceName;
    QString m_protocol;
};

} // namespace myairshow
