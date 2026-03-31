#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHostAddress>
#include <QHash>
#include <QSemaphore>
#include <functional>
#include <memory>

namespace airshow {

class AppSettings;

// Per-request approval state (Pattern 1 from RESEARCH.md).
// Instances are created on the protocol callback thread, shared via shared_ptr
// so that both the protocol thread (blocking on semaphore) and the Qt thread
// (releasing semaphore via resolveApproval) can safely access it.
struct ApprovalRequest {
    QString    requestId;
    QString    deviceName;
    QString    protocol;
    QString    deviceIdentifier;
    QSemaphore semaphore{0};    // Starts locked; released by resolveApproval()
    bool       approved{false};
};

// ---------------------------------------------------------------------------
// SecurityManager
//
// Central security layer for incoming connections. All protocol handlers call
// checkConnection() or checkConnectionAsync() before establishing a session.
//
// Thread model:
//   - checkConnection()       — called from protocol callback threads (blocks)
//   - checkConnectionAsync()  — called from Qt main thread (non-blocking)
//   - resolveApproval()       — called from QML via Q_INVOKABLE, Qt thread only
//   - requestApproval signal  — emitted on Qt thread via QueuedConnection dispatch
//
// The QHash members (m_pendingRequests, m_asyncCallbacks) are ONLY accessed
// from the Qt thread. The protocol thread holds a shared_ptr to an
// ApprovalRequest and reads req->approved only after tryAcquire() provides the
// memory barrier. This is safe by design (RESEARCH.md Pattern 1 thread safety note).
// ---------------------------------------------------------------------------
class SecurityManager : public QObject {
    Q_OBJECT

public:
    explicit SecurityManager(AppSettings& settings, QObject* parent = nullptr);

    // --- Synchronous approval (blocks calling thread) ----------------------
    //
    // Designed for protocol callbacks that run on non-Qt threads (UxPlay RAOP
    // thread, libupnp thread pool). Blocks on QSemaphore until:
    //   (a) resolveApproval() is called from QML and releases the semaphore, or
    //   (b) the timeout expires (auto-deny).
    //
    // Returns true if the connection should be allowed.
    bool checkConnection(const QString& deviceName,
                         const QString& protocol,
                         const QString& deviceIdentifier);

    // Alias used in tests to distinguish the sync variant explicitly.
    bool checkConnectionSync(const QString& deviceName,
                             const QString& protocol,
                             const QString& deviceIdentifier);

    // --- Asynchronous approval (non-blocking) ------------------------------
    //
    // For Cast which runs entirely on the Qt main thread (RESEARCH.md Pitfall 1).
    // Fast-path logic is the same; if a prompt is needed, stores callback keyed
    // by requestId and emits requestApproval. resolveApproval() invokes the
    // callback on the Qt thread.
    void checkConnectionAsync(const QString& deviceName,
                              const QString& protocol,
                              const QString& deviceIdentifier,
                              std::function<void(bool)> callback);

    // --- Network restriction (SEC-03) -------------------------------------
    //
    // Returns true for RFC1918 private, link-local, and loopback addresses.
    // Uses Qt6 API: isLoopback() || isPrivateUse() || isLinkLocal()
    // No manual CIDR arithmetic needed (RESEARCH.md Pattern 4).
    static bool isLocalNetwork(const QHostAddress& addr);

    // Returns non-loopback, non-VPN local addresses for listener binding (D-09).
    // Skips interfaces whose names start with tun, wg, vpn, ppp, tap (Pattern 5).
    static QList<QHostAddress> localNetworkAddresses();

    // --- PIN delegation ---------------------------------------------------
    bool    isPinEnabled() const;
    QString pin() const;

    // --- Testability hook -------------------------------------------------
    // Override the approval timeout (milliseconds). Default: 15000 (15 s).
    void setApprovalTimeoutMs(int ms);

    // --- QML invokable slot -----------------------------------------------
    //
    // Called by the QML approval dialog. Resolves pending sync or async request.
    // If approved, the device identifier is added to the trusted list so future
    // connections skip the prompt.
    Q_INVOKABLE void resolveApproval(const QString& requestId, bool approved);

signals:
    // Emitted on the Qt thread via QueuedConnection when a new connection needs
    // user approval. QML connects this signal to show the approval dialog.
    void requestApproval(const QString& requestId,
                         const QString& deviceName,
                         const QString& protocol);

private:
    // Check if a device identifier is already in the trusted list.
    bool isTrusted(const QString& deviceIdentifier) const;

    // VPN interface heuristic: returns true for tun*, wg*, vpn*, ppp*, tap*
    static bool isVpnInterface(const QString& interfaceName);

    AppSettings& m_settings;
    int          m_approvalTimeoutMs{15000};

    // Both maps are Qt-thread-only (accessed only inside invokeMethod lambdas
    // and resolveApproval). The semaphore in each ApprovalRequest acts as the
    // cross-thread synchronization primitive.
    QHash<QString, std::shared_ptr<ApprovalRequest>>  m_pendingRequests;
    QHash<QString, std::function<void(bool)>>          m_asyncCallbacks;
};

} // namespace airshow
