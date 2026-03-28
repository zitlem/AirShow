#pragma once
#include <string>
#include <upnp/upnp.h>

namespace myairshow {

class AppSettings;

// DLNA SSDP advertisement using libupnp (pupnp).
// Implements UPnP MediaRenderer:1 device advertisement.
// In Phase 2, all SOAP actions return 501 Not Implemented.
// Phase 5 will replace the SOAP callback with real AVTransport logic.
class UpnpAdvertiser {
public:
    // deviceXmlTemplatePath: path to resources/dlna/MediaRenderer.xml
    UpnpAdvertiser(AppSettings* settings, const std::string& deviceXmlTemplatePath);
    ~UpnpAdvertiser();

    // Start SSDP advertisement. Returns false if libupnp unavailable or init fails.
    bool start();

    // Stop SSDP advertisement and clean up libupnp.
    void stop();

    bool isRunning() const;

private:
    // SOAP action callback — all actions return 501 Not Implemented in Phase 2.
    // Signature matches Upnp_FunPtr exactly (libupnp/Callback.h).
    static int upnpCallback(Upnp_EventType_e eventType,
                            const void* event,
                            void* cookie);

    // Write device XML with substituted friendlyName and UDN to a temp file.
    // Returns path to the temp file, or empty string on failure.
    std::string writeRuntimeXml(const std::string& receiverName,
                                const std::string& udn) const;

    AppSettings*  m_settings;
    std::string   m_templatePath;
    int           m_deviceHandle = -1;
    bool          m_running      = false;
    std::string   m_runtimeXmlPath;  // path to temp XML written at start()
};

} // namespace myairshow
