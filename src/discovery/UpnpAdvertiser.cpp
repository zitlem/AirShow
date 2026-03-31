#include "discovery/UpnpAdvertiser.h"
#include "protocol/DlnaHandler.h"
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

namespace airshow {

UpnpAdvertiser::UpnpAdvertiser(AppSettings* settings,
                               const std::string& deviceXmlTemplatePath)
    : m_settings(settings)
    , m_templatePath(deviceXmlTemplatePath) {}

UpnpAdvertiser::~UpnpAdvertiser() {
    stop();
}

void UpnpAdvertiser::setDlnaHandler(DlnaHandler* handler) {
    m_dlnaHandler = handler;
}

// SCPD XML content as inline static string literals — written to temp dir at start()
// so libupnp's built-in HTTP server serves them alongside the device XML (Pitfall 2).
// Content matches the resource files in resources/dlna/ (D-11).

static const char* kAvtScpdContent =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action><name>SetAVTransportURI</name></action>\n"
    "    <action><name>Play</name></action>\n"
    "    <action><name>Stop</name></action>\n"
    "    <action><name>Pause</name></action>\n"
    "    <action><name>Seek</name></action>\n"
    "    <action><name>GetTransportInfo</name></action>\n"
    "    <action><name>GetPositionInfo</name></action>\n"
    "    <action><name>GetMediaInfo</name></action>\n"
    "    <action><name>GetDeviceCapabilities</name></action>\n"
    "    <action><name>GetTransportSettings</name></action>\n"
    "    <action><name>GetCurrentTransportActions</name></action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>TransportStatus</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SeekMode</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SeekTarget</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>RelativeTimePosition</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteTimePosition</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RelativeCounterPosition</name><dataType>i4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteCounterPosition</name><dataType>i4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NumberOfTracks</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrack</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackDuration</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentMediaDuration</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PlaybackStorageMedium</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentPlayMode</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordStorageMedium</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PossiblePlaybackStorageMedia</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PossibleRecordStorageMedia</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PossibleRecordQualityModes</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordMediumWriteStatus</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTransportActions</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentRecordQualityMode</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentMediaCategory</name><dataType>string</dataType></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static const char* kRcScpdContent =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action><name>SetVolume</name></action>\n"
    "    <action><name>GetVolume</name></action>\n"
    "    <action><name>SetMute</name></action>\n"
    "    <action><name>GetMute</name></action>\n"
    "    <action><name>ListPresets</name></action>\n"
    "    <action><name>SelectPreset</name></action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>Mute</name><dataType>boolean</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PresetNameList</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_PresetName</name><dataType>string</dataType></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static const char* kCmScpdContent =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action><name>GetProtocolInfo</name></action>\n"
    "    <action><name>GetCurrentConnectionInfo</name></action>\n"
    "    <action><name>GetCurrentConnectionIDs</name></action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

void UpnpAdvertiser::writeScpdFiles() const {
    // Write inline SCPD content to temp directory so libupnp's HTTP server serves them.
    // The URLs in MediaRenderer.xml match: /avt-scpd.xml, /rc-scpd.xml, /cm-scpd.xml
    const std::string tmpDir = QDir::tempPath().toStdString();

    struct ScpdFile { const char* name; const char* content; };
    ScpdFile files[] = {
        {"avt-scpd.xml", kAvtScpdContent},
        {"rc-scpd.xml",  kRcScpdContent},
        {"cm-scpd.xml",  kCmScpdContent},
    };
    for (auto& f : files) {
        std::string path = tmpDir + "/" + f.name;
        std::ofstream out(path);
        if (out.is_open()) {
            out << f.content;
        } else {
            g_warning("UpnpAdvertiser::writeScpdFiles: failed to write %s", path.c_str());
        }
    }
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

    // Write SCPD XML files to temp dir BEFORE UpnpRegisterRootDevice (Pitfall 2).
    // libupnp's built-in HTTP server serves all files in the same directory as the
    // device XML. The SCPD URLs (/avt-scpd.xml etc.) must exist before registration.
    writeScpdFiles();

    // Register device description. libupnp starts an HTTP server to serve the XML.
    // Pass m_dlnaHandler as cookie so upnpCallback can route SOAP actions to it (D-02).
    ret = UpnpRegisterRootDevice(
        m_runtimeXmlPath.c_str(),
        &UpnpAdvertiser::upnpCallback,
        m_dlnaHandler,
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
    // Clean up SCPD temp files
    const std::string tmpDir = QDir::tempPath().toStdString();
    std::remove((tmpDir + "/avt-scpd.xml").c_str());
    std::remove((tmpDir + "/rc-scpd.xml").c_str());
    std::remove((tmpDir + "/cm-scpd.xml").c_str());
    m_running = false;
}

bool UpnpAdvertiser::isRunning() const {
    return m_running;
}

// static
int UpnpAdvertiser::upnpCallback(Upnp_EventType_e eventType,
                                 const void* event,
                                 void* cookie) {
    // Phase 5: Route SOAP action events to DlnaHandler via cookie pointer (Pattern 1).
    // cookie is the DlnaHandler* passed to UpnpRegisterRootDevice as the user data.
    if (eventType == UPNP_CONTROL_ACTION_REQUEST && cookie) {
        auto* handler = static_cast<airshow::DlnaHandler*>(cookie);
        return handler->handleSoapAction(event);
    }
    if (eventType == UPNP_CONTROL_ACTION_REQUEST) {
        // No handler registered — return 501 Not Implemented
        g_debug("UpnpAdvertiser: SOAP action received but no DlnaHandler set");
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
    replaceAll(xml, "<friendlyName>AirShow</friendlyName>",
                    "<friendlyName>" + receiverName + "</friendlyName>");
    replaceAll(xml, "<UDN>uuid:00000000-0000-0000-0000-000000000000</UDN>",
                    "<UDN>" + udn + "</UDN>");

    // Write to temp file in system temp directory
    std::string tmpPath = QDir::tempPath().toStdString() + "/airshow_dlna.xml";
    std::ofstream out(tmpPath);
    if (!out.is_open()) {
        g_critical("UpnpAdvertiser: cannot write runtime XML to %s", tmpPath.c_str());
        return {};
    }
    out << xml;
    return tmpPath;
}

} // namespace airshow
