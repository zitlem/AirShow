#pragma once
#include <string>
#include <upnp/upnp.h>

namespace airshow {

class AppSettings;
class DlnaHandler;  // forward declaration for SOAP routing (D-02)

// DLNA SSDP advertisement using libupnp (pupnp).
// Implements UPnP MediaRenderer:1 device advertisement.
// Phase 5: Routes SOAP action events to DlnaHandler via cookie pointer (D-02).
class UpnpAdvertiser {
public:
    // deviceXmlTemplatePath: path to resources/dlna/MediaRenderer.xml
    UpnpAdvertiser(AppSettings* settings, const std::string& deviceXmlTemplatePath);
    ~UpnpAdvertiser();

    // Start SSDP advertisement. Returns false if libupnp unavailable or init fails.
    // Writes SCPD XML files to temp dir before registering root device (Pitfall 2).
    bool start();

    // Stop SSDP advertisement and clean up libupnp.
    void stop();

    bool isRunning() const;

    // Set DlnaHandler for SOAP action routing. Must be called before start().
    // DlnaHandler pointer is threaded as cookie into UpnpRegisterRootDevice (Pattern 1).
    void setDlnaHandler(DlnaHandler* handler);

    // Write SCPD XML files to temp dir so libupnp's HTTP server serves them.
    // Called by start() — also callable independently for testing.
    void writeScpdFiles() const;

private:
    // SOAP action callback — routes UPNP_CONTROL_ACTION_REQUEST to DlnaHandler.
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
    int           m_deviceHandle  = -1;
    bool          m_running       = false;
    std::string   m_runtimeXmlPath;  // path to temp XML written at start()
    DlnaHandler*  m_dlnaHandler   = nullptr;  // for SOAP routing via cookie (D-02)
};

} // namespace airshow
