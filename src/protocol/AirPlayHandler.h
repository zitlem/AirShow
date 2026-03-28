#pragma once
#include "protocol/ProtocolHandler.h"
#include <string>
#include <cstdint>
#include <QObject>

// Forward-declare UxPlay's opaque RAOP handle only — stream.h types are
// anonymous structs and cannot be forward-declared; they stay in the .cpp.
struct raop_s;
typedef struct raop_s raop_t;

namespace myairshow {

class MediaPipeline;
class ConnectionBridge;
class DiscoveryManager;

// AirPlay 2 screen mirroring receiver handler (D-02).
// Wraps UxPlay's RAOP server and implements ProtocolHandler.
// Feeds H.264 video and AAC/ALAC audio into MediaPipeline via appsrc (D-03).
// Single-session model: one mirror at a time (D-08); new connection replaces old (D-09).
class AirPlayHandler : public ProtocolHandler {
public:
    // connectionBridge: for HUD updates (D-10). Must outlive this object.
    // discoveryManager: for pk TXT record update (Pitfall 1). Must outlive this object.
    // deviceId: MAC address string (e.g., "AA:BB:CC:DD:EE:FF") matching DiscoveryManager's deviceid TXT.
    // keyfilePath: persistent path for Ed25519 keypair (e.g., QStandardPaths AppDataLocation + "/airplay.key").
    AirPlayHandler(ConnectionBridge* connectionBridge,
                   DiscoveryManager* discoveryManager,
                   const std::string& deviceId,
                   const std::string& keyfilePath);
    ~AirPlayHandler() override;

    // ProtocolHandler interface
    bool start() override;
    void stop() override;
    std::string name() const override { return "airplay"; }
    bool isRunning() const override { return m_running; }
    void setMediaPipeline(MediaPipeline* pipeline) override;

    // Internal callbacks invoked from C trampolines in AirPlayHandler.cpp.
    // Public so the file-scope trampoline functions can call them without friend declarations.
    // These are NOT part of the ProtocolHandler interface — do not call from application code.
    void onVideoFrame(void* data);
    void onAudioFrame(void* data);
    void onConnectionInit();
    void onConnectionDestroy();
    void onConnTeardown(bool* teardown_96, bool* teardown_110);
    void onAudioGetFormat(unsigned char* ct, unsigned short* spf,
                          bool* usingScreen, bool* isMedia, uint64_t* audioFormat);
    void onReportClientRequest(char* deviceid, char* model, char* devicename, bool* admit);

    // Read hex-encoded Ed25519 public key from PEM keyfile after raop_init2 generates it.
    // Returns 64-char lowercase hex string (32 bytes = ED25519_KEY_SIZE) or empty on failure.
    std::string readPublicKeyFromKeyfile() const;

private:
    raop_t*           m_raop             = nullptr;
    MediaPipeline*    m_pipeline         = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    DiscoveryManager* m_discoveryManager = nullptr;
    // GstElement pointers stored as void* to keep GStreamer headers out of this header.
    // Cast to GstElement* in the .cpp where GStreamer headers are included.
    void*             m_videoAppsrc      = nullptr;
    void*             m_audioAppsrc      = nullptr;
    uint64_t          m_basetime         = 0;
    bool              m_basetimeSet      = false;
    std::string       m_deviceId;
    std::string       m_keyfilePath;
    std::string       m_currentDeviceName;  // set by onReportClientRequest
    bool              m_running          = false;
    bool              m_audioCapsSet     = false;
};

} // namespace myairshow
