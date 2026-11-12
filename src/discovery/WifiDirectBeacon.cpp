#include "discovery/WifiDirectBeacon.h"

#include <QSysInfo>

#ifdef __linux__
// Qt's `signals` keyword macro collides with a struct member in GDBus headers
#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")
#include <QMetaObject>
#include <QObject>
#include <QSettings>
#include <string>
#endif

namespace airshow {

namespace {

void appendBe16(QByteArray& out, uint16_t v) {
    out.append(static_cast<char>(v >> 8));
    out.append(static_cast<char>(v & 0xff));
}

} // namespace

// static
QByteArray WifiDirectBeacon::buildMiceVendorExtension(const QString& hostName) {
    // [MS-MICE] 2.2.8: Vendor Extension = Microsoft vendor ID + attributes, each
    // AttributeID (2 bytes) + Length (2 bytes) + value, big-endian.
    QByteArray ext = QByteArray::fromHex("000137");

    // Capability attribute (0x2001), 1 byte: bit 0 (A) MiracastOverInfrastructure
    // supported; bits 2-4 (C) Version = 1. No stream encryption / PIN support.
    appendBe16(ext, 0x2001);
    appendBe16(ext, 1);
    ext.append(static_cast<char>(0x01 | (0x1 << 2)));  // 0x05

    // Host Name attribute (0x2002): the name Windows resolves to reach TCP 7250
    const QByteArray host = hostName.toUtf8();
    appendBe16(ext, 0x2002);
    appendBe16(ext, static_cast<uint16_t>(host.size()));
    ext.append(host);
    return ext;
}

// static
QByteArray WifiDirectBeacon::buildWfdIes(uint16_t rtspPort) {
    QByteArray ies;
    // Subelement 0: WFD Device Information (6 bytes)
    ies.append('\x00');
    appendBe16(ies, 6);
    appendBe16(ies, 0x0011);    // primary sink, available for WFD session
    appendBe16(ies, rtspPort);  // session management control port
    appendBe16(ies, 300);       // max throughput, Mbps
    // Subelement 1: Associated BSSID (6 bytes, none)
    ies.append('\x01');
    appendBe16(ies, 6);
    ies.append(QByteArray(6, '\0'));
    // Subelement 6: Coupled Sink Information (7 bytes, not coupled)
    ies.append('\x06');
    appendBe16(ies, 7);
    ies.append(QByteArray(7, '\0'));
    return ies;
}

#ifdef __linux__

namespace {
constexpr const char* kService   = "fi.w1.wpa_supplicant1";
constexpr const char* kRootPath  = "/fi/w1/wpa_supplicant1";
constexpr const char* kIfaceIf   = "fi.w1.wpa_supplicant1.Interface";
constexpr const char* kP2PIf     = "fi.w1.wpa_supplicant1.Interface.P2PDevice";
constexpr const char* kPropsIf   = "org.freedesktop.DBus.Properties";
constexpr int         kTimeoutMs = 2000;
// WFD session management (RTSP) port advertised in the WFD IE. Same value as
// MiracastHandler::kRtspPort; not included here to keep Qt Network out of tests.
constexpr uint16_t    kWfdRtspPort = 7236;

// QSettings keys holding the P2P settings to restore (survive a crash/kill)
constexpr const char* kRestoreIfname = "miracast/wifiDirectRestore/ifname";
constexpr const char* kRestoreConfig = "miracast/wifiDirectRestore/config";  // a{sv}, printed
constexpr const char* kRestoreWfd    = "miracast/wifiDirectRestore/wfd";     // ay, printed

GVariant* bytesVariant(const QByteArray& b) {
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, b.constData(),
                                     static_cast<gsize>(b.size()), 1);
}

GVariant* listenVariant(int periodMs, int intervalMs) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "period", g_variant_new_int32(periodMs));
    g_variant_builder_add(&b, "{sv}", "interval", g_variant_new_int32(intervalMs));
    return g_variant_new("(a{sv})", &b);
}

// P2P device names are limited to 32 octets; cut on a character boundary.
QByteArray p2pDeviceName(const QString& name) {
    QString n = name;
    while (n.toUtf8().size() > 32) n.chop(1);
    return n.toUtf8();
}

// Returns `exts` (aay, consumed) without MS-MICE vendor extensions. One found in
// the "original" settings was left by an AirShow run whose saved settings were
// lost (e.g. killed before this was persisted); restoring it would keep the
// receiver advertised after AirShow exits.
GVariant* withoutMiceExtension(GVariant* exts) {
    static const QByteArray kMicePrefix = QByteArray::fromHex("0001372001");  // MS OUI + Capability
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("aay"));
    GVariantIter it;
    g_variant_iter_init(&it, exts);
    while (GVariant* ext = g_variant_iter_next_value(&it)) {
        gsize len = 0;
        const auto* data = static_cast<const char*>(
            g_variant_get_fixed_array(ext, &len, sizeof(guchar)));
        if (QByteArray::fromRawData(data, static_cast<qsizetype>(len)).startsWith(kMicePrefix)) {
            g_message("WifiDirectBeacon: removing an MS-MICE vendor extension left by a "
                      "previous run");
        } else {
            g_variant_builder_add_value(&b, ext);
        }
        g_variant_unref(ext);
    }
    g_variant_unref(exts);
    return g_variant_ref_sink(g_variant_builder_end(&b));
}

// Human-readable reason for a failed wpa_supplicant D-Bus call.
std::string explain(GError* err) {
    if (!err) return "unknown error";
    gchar* remote = g_dbus_error_get_remote_error(err);
    std::string name = remote ? remote : "";
    g_free(remote);
    if (name == "org.freedesktop.DBus.Error.AccessDenied") {
        return "no permission to use wpa_supplicant's D-Bus API. Add your user to the "
               "'netdev' group (sudo usermod -aG netdev $USER) and log in again";
    }
    if (name == "org.freedesktop.DBus.Error.ServiceUnknown") {
        return "wpa_supplicant is not running (or not on D-Bus)";
    }
    return err->message;
}
} // namespace

// Runs on WifiDirectBeacon::m_thread; only touched from there (plus construction
// and destruction while the thread is not running).
struct WifiDirectBeacon::Impl : QObject {
    explicit Impl(WifiDirectBeacon* o) : owner(o) {}

    WifiDirectBeacon* owner;
    GDBusConnection*  bus = nullptr;
    std::string       ifacePath;
    std::string       ifname;
    GVariant*         savedConfig = nullptr;  // a{sv}: the keys we change
    GVariant*         savedWfd    = nullptr;  // ay

    ~Impl() override {
        clearSaved();
        if (bus) g_object_unref(bus);
    }

    void clearSaved() {
        if (savedConfig) g_variant_unref(savedConfig);
        if (savedWfd)    g_variant_unref(savedWfd);
        savedConfig = savedWfd = nullptr;
    }

    // Returns the unwrapped property value (caller unrefs) or nullptr.
    GVariant* get(const std::string& path, const char* iface, const char* prop, GError** err) {
        GVariant* r = g_dbus_connection_call_sync(
            bus, kService, path.c_str(), kPropsIf, "Get",
            g_variant_new("(ss)", iface, prop), G_VARIANT_TYPE("(v)"),
            G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, err);
        if (!r) return nullptr;
        GVariant* v = nullptr;
        g_variant_get(r, "(v)", &v);
        g_variant_unref(r);
        return v;
    }

    bool set(const std::string& path, const char* iface, const char* prop,
             GVariant* value, GError** err) {
        GVariant* r = g_dbus_connection_call_sync(
            bus, kService, path.c_str(), kPropsIf, "Set",
            g_variant_new("(ssv)", iface, prop, value), nullptr,
            G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, err);
        if (r) g_variant_unref(r);
        return r != nullptr;
    }

    bool call(const char* method, GVariant* params, GError** err) {
        GVariant* r = g_dbus_connection_call_sync(
            bus, kService, ifacePath.c_str(), kP2PIf, method, params, nullptr,
            G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, err);
        if (r) g_variant_unref(r);
        return r != nullptr;
    }

    bool connectBus(GError** err) {
        if (!bus) bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, err);
        return bus != nullptr;
    }

    // Pick the station interface that owns a P2P device (e.g. wlp2s0, not the
    // p2p-dev-* / p2p-*-N interfaces wpa_supplicant creates for P2P itself).
    bool findInterface(GError** err) {
        GVariant* list = get(kRootPath, kService, "Interfaces", err);
        if (!list) return false;
        ifacePath.clear();
        GVariantIter it;
        const gchar* path = nullptr;
        g_variant_iter_init(&it, list);
        while (g_variant_iter_next(&it, "&o", &path)) {
            GVariant* nameV = get(path, kIfaceIf, "Ifname", nullptr);
            if (!nameV) continue;
            const std::string name = g_variant_get_string(nameV, nullptr);
            g_variant_unref(nameV);
            if (name.rfind("p2p-", 0) == 0) continue;
            GVariant* cfg = get(path, kP2PIf, "P2PDeviceConfig", nullptr);
            if (!cfg) continue;  // no P2P support on this interface
            g_variant_unref(cfg);
            ifacePath = path;
            ifname = name;
            break;
        }
        g_variant_unref(list);
        if (ifacePath.empty()) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                "no Wi-Fi interface with Wi-Fi Direct (P2P) support");
            return false;
        }
        return true;
    }

    // --- Persistence of the settings to restore ---------------------------

    static bool hasPersisted() {
        return QSettings().contains(QLatin1String(kRestoreIfname));
    }

    static void clearPersisted() {
        QSettings s;
        s.remove(QStringLiteral("miracast/wifiDirectRestore"));
        s.sync();
    }

    void persist() const {
        gchar* cfg = g_variant_print(savedConfig, TRUE);
        gchar* wfd = g_variant_print(savedWfd, TRUE);
        QSettings s;
        s.setValue(QLatin1String(kRestoreIfname), QString::fromStdString(ifname));
        s.setValue(QLatin1String(kRestoreConfig), QString::fromUtf8(cfg));
        s.setValue(QLatin1String(kRestoreWfd),    QString::fromUtf8(wfd));
        s.sync();
        g_free(cfg);
        g_free(wfd);
    }

    // Loads settings saved by a run that never reached stop(), if they belong
    // to the interface in use.
    bool loadPersisted() {
        QSettings s;
        if (s.value(QLatin1String(kRestoreIfname)).toString().toStdString() != ifname) {
            return false;
        }
        const QByteArray cfgText = s.value(QLatin1String(kRestoreConfig)).toString().toUtf8();
        const QByteArray wfdText = s.value(QLatin1String(kRestoreWfd)).toString().toUtf8();
        GVariant* cfg = g_variant_parse(G_VARIANT_TYPE("a{sv}"), cfgText.constData(),
                                        nullptr, nullptr, nullptr);
        GVariant* wfd = g_variant_parse(G_VARIANT_TYPE("ay"), wfdText.constData(),
                                        nullptr, nullptr, nullptr);
        if (!cfg || !wfd) {
            if (cfg) g_variant_unref(cfg);
            if (wfd) g_variant_unref(wfd);
            g_warning("WifiDirectBeacon: ignoring unreadable saved Wi-Fi Direct settings");
            clearPersisted();
            return false;
        }
        clearSaved();
        savedConfig = g_variant_ref_sink(cfg);
        savedWfd    = g_variant_ref_sink(wfd);
        return true;
    }

    // Saves the P2P settings apply() changes, before it changes them. Returns
    // false if they can't be read: then nothing is changed, since it could not
    // be undone.
    bool saveOriginals(GError** err) {
        if (savedConfig) return true;
        if (loadPersisted()) {
            g_message("WifiDirectBeacon: a previous run did not exit cleanly; will restore "
                      "the Wi-Fi Direct settings it saved");
            return true;
        }
        GVariant* cfg = get(ifacePath, kP2PIf, "P2PDeviceConfig", err);
        if (!cfg) return false;
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
        for (const char* key : {"DeviceName", "PrimaryDeviceType", "VendorExtension"}) {
            if (GVariant* v = g_variant_lookup_value(cfg, key, nullptr)) {
                if (std::string(key) == "VendorExtension") {
                    v = withoutMiceExtension(v);
                }
                g_variant_builder_add(&b, "{sv}", key, v);
                g_variant_unref(v);
            } else if (std::string(key) == "VendorExtension") {
                // Absent means none; restoring must still remove ours
                g_variant_builder_add(&b, "{sv}", key,
                                      g_variant_new_array(G_VARIANT_TYPE("ay"), nullptr, 0));
            }
        }
        savedConfig = g_variant_ref_sink(g_variant_builder_end(&b));
        g_variant_unref(cfg);

        // Unreadable / unset WFD IEs: restore to empty so ours are removed
        GVariant* wfd = get(kRootPath, kService, "WFDIEs", nullptr);
        savedWfd = wfd ? wfd : g_variant_ref_sink(bytesVariant({}));

        persist();
        return true;
    }

    // --- Operations (worker thread) ---------------------------------------

    bool apply(const QString& deviceName, std::string& why) {
        GError* err = nullptr;
        auto fail = [&](const char* step) {
            why = std::string(step) + ": " + explain(err);
            g_clear_error(&err);
            return false;
        };

        if (!connectBus(&err))       return fail("system D-Bus");
        if (!findInterface(&err))    return fail("wpa_supplicant");
        if (!saveOriginals(&err))    return fail("reading the current P2P settings");

        const QString host = QSysInfo::machineHostName();

        GVariantBuilder ext;
        g_variant_builder_init(&ext, G_VARIANT_TYPE("aay"));
        g_variant_builder_add_value(&ext, bytesVariant(buildMiceVendorExtension(host)));

        GVariantBuilder cfg;
        g_variant_builder_init(&cfg, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&cfg, "{sv}", "DeviceName",
                              g_variant_new_string(p2pDeviceName(deviceName).constData()));
        // WPS primary device type: category 7 (Displays), WPS OUI, subcategory 1
        g_variant_builder_add(&cfg, "{sv}", "PrimaryDeviceType",
                              bytesVariant(QByteArray::fromHex("00070050f2040001")));
        g_variant_builder_add(&cfg, "{sv}", "VendorExtension", g_variant_builder_end(&ext));

        if (!set(ifacePath, kP2PIf, "P2PDeviceConfig", g_variant_builder_end(&cfg), &err))
            return fail("setting P2P device config");
        if (!set(kRootPath, kService, "WFDIEs",
                 bytesVariant(buildWfdIes(kWfdRtspPort)), &err))
            return fail("setting Wi-Fi Display IEs");

        // Listen 500 ms of every 1000 ms so Windows' P2P scan gets probe responses
        if (!call("ExtendedListen", listenVariant(500, 1000), &err))
            return fail("starting P2P listen");

        why = "via " + ifname + " (host " + host.toStdString() + ")";
        return true;
    }

    // Puts back the saved settings and forgets them. Keeps them (in memory and
    // QSettings) if wpa_supplicant can't be reached, so a later run can retry.
    void restore() {
        if (!savedConfig || !bus || ifacePath.empty()) return;

        GError* err = nullptr;
        call("ExtendedListen", listenVariant(0, 0), nullptr);
        bool ok = set(ifacePath, kP2PIf, "P2PDeviceConfig", savedConfig, &err);
        if (ok) ok = set(kRootPath, kService, "WFDIEs", savedWfd, &err);
        if (!ok) {
            g_warning("WifiDirectBeacon: could not restore the Wi-Fi Direct settings (%s); "
                      "will retry on the next start", explain(err).c_str());
            g_clear_error(&err);
            return;
        }
        clearSaved();
        clearPersisted();
    }

    void recoverStale() {
        if (!hasPersisted()) return;
        GError* err = nullptr;
        if (!connectBus(&err) || !findInterface(&err) || !loadPersisted()) {
            g_clear_error(&err);
            return;  // try again on a later run
        }
        g_message("WifiDirectBeacon: restoring Wi-Fi Direct settings left by a previous run");
        restore();
    }
};

WifiDirectBeacon::WifiDirectBeacon() : d(std::make_unique<Impl>(this)) {
    m_thread.setObjectName(QStringLiteral("WifiDirectBeacon"));
    // wpa_supplicant may restart, or another client may reset the P2P device;
    // re-apply periodically (idempotent).
    m_refreshTimer.setInterval(60 * 1000);
    m_refreshTimer.callOnTimeout([this] { postApply(/*logResult=*/false); });
}

WifiDirectBeacon::~WifiDirectBeacon() {
    stop();
    if (m_thread.isRunning()) {
        // Hand d back to this thread so it is destroyed where it lives
        QThread* here = QThread::currentThread();
        QMetaObject::invokeMethod(d.get(), [this, here] { d->moveToThread(here); },
                                  Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }
}

void WifiDirectBeacon::ensureWorker() {
    if (m_thread.isRunning()) return;
    d->moveToThread(&m_thread);
    m_thread.start();
}

void WifiDirectBeacon::postApply(bool logResult) {
    // Skip timer refreshes while one is still queued (wpa_supplicant stuck)
    if (!logResult && m_applyPending.exchange(true)) return;
    if (logResult) m_applyPending = true;
    const QString name = m_deviceName;
    QMetaObject::invokeMethod(d.get(), [this, name, logResult] {
        m_applyPending = false;
        std::string why;
        const bool ok  = d->apply(name, why);
        const bool was = m_active.exchange(ok);
        if (logResult) {
            if (ok) g_message("WifiDirectBeacon: discoverable in Windows Connect (Win+K) as "
                              "'%s' %s", qPrintable(name), why.c_str());
            else    g_message("WifiDirectBeacon: Windows Miracast discovery (Win+K) "
                              "unavailable — %s", why.c_str());
        } else if (ok != was) {
            if (ok) g_message("WifiDirectBeacon: discoverable again %s", why.c_str());
            else    g_warning("WifiDirectBeacon: lost Wi-Fi Direct discovery (%s); retrying",
                              why.c_str());
        }
    }, Qt::QueuedConnection);
}

void WifiDirectBeacon::start(const QString& deviceName) {
    m_deviceName = deviceName;
    ensureWorker();
    postApply(/*logResult=*/true);
    m_refreshTimer.start();
}

void WifiDirectBeacon::rename(const QString& deviceName) {
    m_deviceName = deviceName;
    if (m_refreshTimer.isActive()) {
        postApply(/*logResult=*/true);
    }
}

void WifiDirectBeacon::recoverStaleState() {
    if (!Impl::hasPersisted()) return;
    ensureWorker();
    QMetaObject::invokeMethod(d.get(), [this] { d->recoverStale(); }, Qt::QueuedConnection);
}

void WifiDirectBeacon::stop() {
    m_refreshTimer.stop();
    if (!m_thread.isRunning()) return;
    // Blocking: runs after any queued apply, and before the process may exit
    QMetaObject::invokeMethod(d.get(), [this] {
        const bool was = m_active.exchange(false);
        d->restore();
        if (was) g_message("WifiDirectBeacon: stopped");
    }, Qt::BlockingQueuedConnection);
}

#else  // !__linux__

struct WifiDirectBeacon::Impl {};

WifiDirectBeacon::WifiDirectBeacon() : d(std::make_unique<Impl>()) {}
WifiDirectBeacon::~WifiDirectBeacon() = default;
void WifiDirectBeacon::start(const QString& deviceName) { m_deviceName = deviceName; }
void WifiDirectBeacon::stop() {}
void WifiDirectBeacon::rename(const QString& deviceName) { m_deviceName = deviceName; }
void WifiDirectBeacon::recoverStaleState() {}
void WifiDirectBeacon::ensureWorker() {}
void WifiDirectBeacon::postApply(bool) {}

#endif

} // namespace airshow
