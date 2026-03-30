// SecurityManager.cpp — Central security layer implementation
// Phase 07 Plan 01: Security & Hardening
//
// Thread model:
//   checkConnection()      — called from any protocol callback thread (blocks on semaphore)
//   checkConnectionAsync() — called from Qt main thread (non-blocking)
//   resolveApproval()      — called from QML (Q_INVOKABLE), runs on Qt main thread
//   requestApproval signal — dispatched to Qt main thread via QueuedConnection

#include "security/SecurityManager.h"
#include "settings/AppSettings.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QMetaObject>
#include <QSemaphore>
#include <QUuid>

namespace myairshow {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SecurityManager::SecurityManager(AppSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{}

// ---------------------------------------------------------------------------
// Testability hook
// ---------------------------------------------------------------------------

void SecurityManager::setApprovalTimeoutMs(int ms) {
    m_approvalTimeoutMs = ms;
}

// ---------------------------------------------------------------------------
// isLocalNetwork() — SEC-03 RFC1918 / link-local / loopback filter
//
// Uses Qt6 API (confirmed in Qt 6.9.2 header):
//   isLoopback()    — 127.0.0.1, ::1
//   isPrivateUse()  — 10/8, 172.16/12, 192.168/16, fc00::/7
//   isLinkLocal()   — 169.254/16, fe80::/10
//
// No manual CIDR arithmetic (RESEARCH.md Pattern 4).
// ---------------------------------------------------------------------------

bool SecurityManager::isLocalNetwork(const QHostAddress& addr) {
    if (addr.isLoopback())    return true;  // 127.x, ::1
    if (addr.isPrivateUse())  return true;  // RFC1918 + IPv6 ULA
    if (addr.isLinkLocal())   return true;  // 169.254/16, fe80::/10
    return false;
}

// ---------------------------------------------------------------------------
// isVpnInterface() — heuristic for VPN tunnel interfaces (D-09)
// Matches: tun*, wg*, vpn*, ppp*, tap*
// ---------------------------------------------------------------------------

bool SecurityManager::isVpnInterface(const QString& name) {
    const QStringList vpnPrefixes = {
        QStringLiteral("tun"),
        QStringLiteral("wg"),
        QStringLiteral("vpn"),
        QStringLiteral("ppp"),
        QStringLiteral("tap")
    };
    for (const QString& prefix : vpnPrefixes) {
        if (name.startsWith(prefix, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// localNetworkAddresses() — non-loopback, non-VPN local addresses (D-09)
// ---------------------------------------------------------------------------

QList<QHostAddress> SecurityManager::localNetworkAddresses() {
    QList<QHostAddress> result;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        // Skip loopback interfaces
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        // Skip VPN interfaces by name heuristic
        if (isVpnInterface(iface.name())) {
            continue;
        }
        // Collect addresses that are local (private or link-local)
        const auto addressEntries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : addressEntries) {
            const QHostAddress addr = entry.ip();
            if (addr.isPrivateUse() || addr.isLinkLocal()) {
                result.append(addr);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// isTrusted() — check if identifier is in the trusted device list
// ---------------------------------------------------------------------------

bool SecurityManager::isTrusted(const QString& deviceIdentifier) const {
    if (deviceIdentifier.isEmpty()) return false;
    return m_settings.trustedDevices().contains(deviceIdentifier);
}

// ---------------------------------------------------------------------------
// checkConnection() — synchronous blocking approval (SEC-01)
//
// Called from protocol callback threads (UxPlay RAOP thread, libupnp pool).
// Blocks on QSemaphore until the user responds or the timeout expires.
// ---------------------------------------------------------------------------

bool SecurityManager::checkConnection(const QString& deviceName,
                                      const QString& protocol,
                                      const QString& deviceIdentifier) {
    // Fast path 1: approval disabled — allow all local connections
    if (!m_settings.requireApproval()) {
        return true;
    }
    // Fast path 2: already trusted — skip prompt
    if (isTrusted(deviceIdentifier)) {
        return true;
    }

    // Slow path: create per-request struct and emit requestApproval on Qt thread
    auto req = std::make_shared<ApprovalRequest>();
    req->requestId       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req->deviceName      = deviceName;
    req->protocol        = protocol;
    req->deviceIdentifier = deviceIdentifier;

    // Dispatch to Qt event loop (QueuedConnection marshals across thread boundary)
    QMetaObject::invokeMethod(this, [this, req]() {
        m_pendingRequests[req->requestId] = req;
        emit requestApproval(req->requestId, req->deviceName, req->protocol);
    }, Qt::QueuedConnection);

    // Block calling thread until resolveApproval() releases or timeout fires
    bool acquired = req->semaphore.tryAcquire(1, m_approvalTimeoutMs);
    return acquired && req->approved;
}

// Alias so tests can refer to the synchronous variant by an explicit name
bool SecurityManager::checkConnectionSync(const QString& deviceName,
                                          const QString& protocol,
                                          const QString& deviceIdentifier) {
    return checkConnection(deviceName, protocol, deviceIdentifier);
}

// ---------------------------------------------------------------------------
// checkConnectionAsync() — non-blocking approval for Qt-main-thread callers
//
// Used by Cast (runs on Qt main thread; blocking would deadlock the event loop).
// Same fast paths as checkConnection(). If a prompt is needed, the callback is
// stored and invoked when resolveApproval() is called from QML.
// ---------------------------------------------------------------------------

void SecurityManager::checkConnectionAsync(const QString& deviceName,
                                           const QString& protocol,
                                           const QString& deviceIdentifier,
                                           std::function<void(bool)> callback) {
    if (!m_settings.requireApproval()) {
        if (callback) callback(true);
        return;
    }
    if (isTrusted(deviceIdentifier)) {
        if (callback) callback(true);
        return;
    }

    // Prompt required — store callback and emit signal
    QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_asyncCallbacks[requestId] = std::move(callback);
    emit requestApproval(requestId, deviceName, protocol);
}

// ---------------------------------------------------------------------------
// resolveApproval() — Q_INVOKABLE, Qt main thread only
//
// Called by QML approval dialog. Handles both sync (semaphore release) and
// async (callback invocation) pending requests.
// ---------------------------------------------------------------------------

void SecurityManager::resolveApproval(const QString& requestId, bool approved) {
    // Handle sync pending request
    auto syncIt = m_pendingRequests.find(requestId);
    if (syncIt != m_pendingRequests.end()) {
        auto req = syncIt.value();
        m_pendingRequests.erase(syncIt);
        req->approved = approved;
        if (approved && !req->deviceIdentifier.isEmpty()) {
            m_settings.addTrustedDevice(req->deviceIdentifier);
        }
        req->semaphore.release(1);  // Unblocks the protocol callback thread
        return;
    }

    // Handle async pending request
    auto asyncIt = m_asyncCallbacks.find(requestId);
    if (asyncIt != m_asyncCallbacks.end()) {
        auto cb = asyncIt.value();
        m_asyncCallbacks.erase(asyncIt);
        if (approved) {
            // Identifier not stored for async path here — callers pass it upstream
            // (CastHandler will store the UUID before calling checkConnectionAsync)
        }
        if (cb) cb(approved);
    }
}

// ---------------------------------------------------------------------------
// PIN delegation
// ---------------------------------------------------------------------------

bool SecurityManager::isPinEnabled() const {
    return m_settings.pinEnabled();
}

QString SecurityManager::pin() const {
    return m_settings.pin();
}

} // namespace myairshow
