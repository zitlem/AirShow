#include "discovery/WifiDirectBeacon.h"

#include <QSysInfo>
#include <glib.h>

#ifdef __linux__
// Qt's `signals` keyword macro collides with a struct member in GDBus headers
#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")
#include <string>
#endif

namespace airshow {

namespace {

void appendBe16(QByteArray& out, uint16_t v) {
    out.append(static_cast<char>(v >> 8));
    out.append(static_cast<char>(v & 0xff));
}

// P2P device names are limited to 32 octets; cut on a character boundary.
QByteArray p2pDeviceName(const QString& name) {
    QString n = name;
    while (n.toUtf8().size() > 32) n.chop(1);
    return n.toUtf8();
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

GVariant* bytesVariant(const QByteArray& b) {
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, b.constData(),
                                     static_cast<gsize>(b.size()), 1);
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

struct WifiDirectBeacon::Impl {
    GDBusConnection* bus = nullptr;
    std::string      ifacePath;
    std::string      ifname;
    GVariant*        savedConfig = nullptr;  // a{sv}: the keys we change
    GVariant*        savedWfd    = nullptr;  // ay

    ~Impl() {
        if (savedConfig) g_variant_unref(savedConfig);
        if (savedWfd)    g_variant_unref(savedWfd);
        if (bus)         g_object_unref(bus);
    }

    // Returns the unwrapped property value (caller unrefs) or nullptr.
    GVariant* get(const std::string& path, const char* iface, const char* prop, GError** err) {
        GVariant* r = g_dbus_connection_call_sync(
            bus, kService, path.c_str(), kPropsIf, "Get",
            g_variant_new("(ss)", iface, prop), G_VARIANT_TYPE("(v)"),
            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, err);
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
            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, err);
        if (r) g_variant_unref(r);
        return r != nullptr;
    }

    bool call(const char* method, GVariant* params, GError** err) {
        GVariant* r = g_dbus_connection_call_sync(
            bus, kService, ifacePath.c_str(), kP2PIf, method, params, nullptr,
            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, err);
        if (r) g_variant_unref(r);
        return r != nullptr;
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

    void saveOriginals() {
        if (savedConfig) return;
        if (GVariant* cfg = get(ifacePath, kP2PIf, "P2PDeviceConfig", nullptr)) {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
            for (const char* key : {"DeviceName", "PrimaryDeviceType", "VendorExtension"}) {
                if (GVariant* v = g_variant_lookup_value(cfg, key, nullptr)) {
                    g_variant_builder_add(&b, "{sv}", key, v);
                    g_variant_unref(v);
                }
            }
            savedConfig = g_variant_ref_sink(g_variant_builder_end(&b));
            g_variant_unref(cfg);
        }
        if (GVariant* wfd = get(kRootPath, kService, "WFDIEs", nullptr)) {
            savedWfd = wfd;
        }
    }
};

WifiDirectBeacon::WifiDirectBeacon() : d(std::make_unique<Impl>()) {
    // wpa_supplicant may restart, or another client may reset the P2P device;
    // re-apply periodically (idempotent).
    m_refreshTimer.setInterval(60 * 1000);
    m_refreshTimer.callOnTimeout([this] {
        const bool ok = apply(/*logFailures=*/false);
        if (ok != m_active) {
            if (ok) g_message("WifiDirectBeacon: discoverable again on %s", d->ifname.c_str());
            else    g_warning("WifiDirectBeacon: lost Wi-Fi Direct discovery; retrying");
        }
        m_active = ok;
    });
}

WifiDirectBeacon::~WifiDirectBeacon() {
    stop();
}

bool WifiDirectBeacon::start(const QString& deviceName) {
    m_deviceName = deviceName;
    m_active = apply(/*logFailures=*/true);
    m_refreshTimer.start();
    return m_active;
}

void WifiDirectBeacon::rename(const QString& deviceName) {
    m_deviceName = deviceName;
    if (m_refreshTimer.isActive()) {
        m_active = apply(/*logFailures=*/true);
    }
}

bool WifiDirectBeacon::apply(bool logFailures) {
    GError* err = nullptr;
    auto fail = [&](const char* step) {
        if (logFailures) {
            g_message("WifiDirectBeacon: Windows Miracast discovery (Win+K) unavailable — "
                      "%s: %s", step, explain(err).c_str());
        }
        g_clear_error(&err);
        return false;
    };

    if (!d->bus) {
        d->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
        if (!d->bus) return fail("system D-Bus");
    }
    if (!d->findInterface(&err)) return fail("wpa_supplicant");
    d->saveOriginals();

    const QString host = QSysInfo::machineHostName();

    GVariantBuilder ext;
    g_variant_builder_init(&ext, G_VARIANT_TYPE("aay"));
    g_variant_builder_add_value(&ext, bytesVariant(buildMiceVendorExtension(host)));

    GVariantBuilder cfg;
    g_variant_builder_init(&cfg, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&cfg, "{sv}", "DeviceName",
                          g_variant_new_string(p2pDeviceName(m_deviceName).constData()));
    // WPS primary device type: category 7 (Displays), WPS OUI, subcategory 1
    g_variant_builder_add(&cfg, "{sv}", "PrimaryDeviceType",
                          bytesVariant(QByteArray::fromHex("00070050f2040001")));
    g_variant_builder_add(&cfg, "{sv}", "VendorExtension", g_variant_builder_end(&ext));

    if (!d->set(d->ifacePath, kP2PIf, "P2PDeviceConfig", g_variant_builder_end(&cfg), &err))
        return fail("setting P2P device config");
    if (!d->set(kRootPath, kService, "WFDIEs", bytesVariant(buildWfdIes(7236)), &err))
        return fail("setting Wi-Fi Display IEs");

    // Listen 500 ms of every 1000 ms so Windows' P2P scan gets probe responses
    GVariantBuilder listen;
    g_variant_builder_init(&listen, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&listen, "{sv}", "period", g_variant_new_int32(500));
    g_variant_builder_add(&listen, "{sv}", "interval", g_variant_new_int32(1000));
    if (!d->call("ExtendedListen", g_variant_new("(a{sv})", &listen), &err))
        return fail("starting P2P listen");

    if (logFailures) {
        g_message("WifiDirectBeacon: discoverable in Windows Connect (Win+K) as '%s' via %s "
                  "(host %s)", qPrintable(m_deviceName), d->ifname.c_str(), qPrintable(host));
    }
    return true;
}

void WifiDirectBeacon::stop() {
    m_refreshTimer.stop();
    if (!d->bus || d->ifacePath.empty()) {
        m_active = false;
        return;
    }

    GVariantBuilder listen;
    g_variant_builder_init(&listen, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&listen, "{sv}", "period", g_variant_new_int32(0));
    g_variant_builder_add(&listen, "{sv}", "interval", g_variant_new_int32(0));
    d->call("ExtendedListen", g_variant_new("(a{sv})", &listen), nullptr);

    if (d->savedConfig) {
        d->set(d->ifacePath, kP2PIf, "P2PDeviceConfig", d->savedConfig, nullptr);
        g_variant_unref(d->savedConfig);
        d->savedConfig = nullptr;
    }
    if (d->savedWfd) {
        d->set(kRootPath, kService, "WFDIEs", d->savedWfd, nullptr);
        g_variant_unref(d->savedWfd);
        d->savedWfd = nullptr;
    }
    if (m_active) g_message("WifiDirectBeacon: stopped");
    m_active = false;
}

#else  // !__linux__

struct WifiDirectBeacon::Impl {};

WifiDirectBeacon::WifiDirectBeacon() : d(std::make_unique<Impl>()) {}
WifiDirectBeacon::~WifiDirectBeacon() = default;
bool WifiDirectBeacon::start(const QString& deviceName) { m_deviceName = deviceName; return false; }
void WifiDirectBeacon::stop() {}
void WifiDirectBeacon::rename(const QString& deviceName) { m_deviceName = deviceName; }
bool WifiDirectBeacon::apply(bool) { return false; }

#endif

} // namespace airshow
