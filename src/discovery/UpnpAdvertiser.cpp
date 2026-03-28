#include "discovery/UpnpAdvertiser.h"
#include "settings/AppSettings.h"

// libupnp headers (upnp.h includes Callback.h transitively)
#include <upnp/upnp.h>
#include <upnp/upnptools.h>

#include <QSettings>
#include <QUuid>
#include <QDir>
#include <glib.h>

#include <fstream>
#include <sstream>
#include <cstdio>

namespace myairshow {

UpnpAdvertiser::UpnpAdvertiser(AppSettings* settings,
                               const std::string& deviceXmlTemplatePath)
    : m_settings(settings)
    , m_templatePath(deviceXmlTemplatePath) {}

UpnpAdvertiser::~UpnpAdvertiser() {
    stop();
}

bool UpnpAdvertiser::start() {
    // Initialize libupnp. nullptr = first non-loopback interface; 0 = auto port.
    // Note: if VPN is active, this may bind to the wrong interface (Pitfall 4).
    // For Phase 2, auto-binding is acceptable. Phase 5 can add interface selection.
    int ret = UpnpInit2(nullptr, 0);
    if (ret != UPNP_E_SUCCESS) {
        g_critical("UpnpAdvertiser: UpnpInit2 failed: %s (%d)",
                   UpnpGetErrorMessage(ret), ret);
        return false;
    }

    // Retrieve or generate stable DLNA UDN
    QSettings s;
    const QString udnKey = "dlna/udn";
    if (!s.contains(udnKey)) {
        s.setValue(udnKey, QUuid::createUuid().toString(QUuid::WithBraces));
        s.sync();
    }
    const std::string udn  = s.value(udnKey).toString().toStdString();
    const std::string name = m_settings->receiverName().toStdString();

    // Write runtime XML with correct friendlyName and UDN
    m_runtimeXmlPath = writeRuntimeXml(name, udn);
    if (m_runtimeXmlPath.empty()) {
        g_critical("UpnpAdvertiser: failed to write runtime device XML");
        UpnpFinish();
        return false;
    }

    // Register device description. libupnp starts an HTTP server to serve the XML.
    ret = UpnpRegisterRootDevice(
        m_runtimeXmlPath.c_str(),
        &UpnpAdvertiser::upnpCallback,
        nullptr,
        &m_deviceHandle
    );
    if (ret != UPNP_E_SUCCESS) {
        g_critical("UpnpAdvertiser: UpnpRegisterRootDevice failed: %s (%d)",
                   UpnpGetErrorMessage(ret), ret);
        UpnpFinish();
        return false;
    }

    // Broadcast SSDP NOTIFY alive messages (100s TTL)
    ret = UpnpSendAdvertisement(m_deviceHandle, 100);
    if (ret != UPNP_E_SUCCESS) {
        g_warning("UpnpAdvertiser: UpnpSendAdvertisement returned: %s (%d)",
                  UpnpGetErrorMessage(ret), ret);
        // Non-fatal: device may still be discoverable via M-SEARCH
    }

    m_running = true;
    g_message("UpnpAdvertiser: DLNA MediaRenderer advertising as '%s'", name.c_str());
    return true;
}

void UpnpAdvertiser::stop() {
    if (m_running && m_deviceHandle >= 0) {
        UpnpUnRegisterRootDevice(m_deviceHandle);
        m_deviceHandle = -1;
    }
    if (m_running) {
        UpnpFinish();
    }
    // Clean up temp XML file
    if (!m_runtimeXmlPath.empty()) {
        std::remove(m_runtimeXmlPath.c_str());
        m_runtimeXmlPath.clear();
    }
    m_running = false;
}

bool UpnpAdvertiser::isRunning() const {
    return m_running;
}

// static
int UpnpAdvertiser::upnpCallback(Upnp_EventType_e eventType,
                                 const void* /*event*/,
                                 void* /*cookie*/) {
    // Phase 2: all SOAP actions return 501 Not Implemented.
    // Phase 5 will replace this with real AVTransport/RenderingControl handling.
    if (eventType == UPNP_CONTROL_ACTION_REQUEST) {
        g_debug("UpnpAdvertiser: SOAP action received — returning 501 Not Implemented");
        // The caller (libupnp) expects UPNP_E_SUCCESS return even for 501 responses.
        // Returning an error code here causes libupnp to send a transport error.
    }
    return UPNP_E_SUCCESS;
}

std::string UpnpAdvertiser::writeRuntimeXml(const std::string& receiverName,
                                             const std::string& udn) const {
    // Read template XML
    std::ifstream tmpl(m_templatePath);
    if (!tmpl.is_open()) {
        g_critical("UpnpAdvertiser: cannot open template XML: %s",
                   m_templatePath.c_str());
        return {};
    }
    std::ostringstream buf;
    buf << tmpl.rdbuf();
    std::string xml = buf.str();

    // Replace static placeholder friendlyName and UDN
    auto replaceAll = [](std::string& s, const std::string& from,
                         const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll(xml, "<friendlyName>MyAirShow</friendlyName>",
                    "<friendlyName>" + receiverName + "</friendlyName>");
    replaceAll(xml, "<UDN>uuid:00000000-0000-0000-0000-000000000000</UDN>",
                    "<UDN>" + udn + "</UDN>");

    // Write to temp file in system temp directory
    std::string tmpPath = QDir::tempPath().toStdString() + "/myairshow_dlna.xml";
    std::ofstream out(tmpPath);
    if (!out.is_open()) {
        g_critical("UpnpAdvertiser: cannot write runtime XML to %s", tmpPath.c_str());
        return {};
    }
    out << xml;
    return tmpPath;
}

} // namespace myairshow
