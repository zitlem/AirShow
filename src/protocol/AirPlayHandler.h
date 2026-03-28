#pragma once
#include "protocol/ProtocolHandler.h"
#include <string>
#include <cstdint>
#include <QObject>

// Forward declarations — avoid including UxPlay headers in public header
struct raop_s;
typedef struct raop_s raop_t;
struct raop_ntp_s;
typedef struct raop_ntp_s raop_ntp_t;
typedef struct {
    bool is_h265;
    int nal_count;
    unsigned char *data;
    int data_len;
    uint64_t ntp_time_local;
    uint64_t ntp_time_remote;
} video_decode_struct;

typedef struct {
    unsigned char *data;
    unsigned char ct;
    int data_len;
    int sync_status;
    uint64_t ntp_time_local;
    uint64_t ntp_time_remote;
    uint32_t rtp_time;
    unsigned short seqnum;
} audio_decode_struct;

struct GstElement;  // GStreamer forward decl

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

private:
    // Static C callback trampolines — cast cls to AirPlayHandler*
    static void sVideoProcess(void* cls, raop_ntp_t* ntp, video_decode_struct* data);
    static void sAudioProcess(void* cls, raop_ntp_t* ntp, audio_decode_struct* data);
    static void sConnInit(void* cls);
    static void sConnDestroy(void* cls);
    static void sConnTeardown(void* cls, bool* teardown_96, bool* teardown_110);
    static void sAudioGetFormat(void* cls, unsigned char* ct, unsigned short* spf,
                                bool* usingScreen, bool* isMedia, uint64_t* audioFormat);
    static void sReportClientRequest(void* cls, char* deviceid, char* model,
                                     char* devicename, bool* admit);

    // Instance methods called by trampolines
    void onVideoFrame(video_decode_struct* data);
    void onAudioFrame(audio_decode_struct* data);
    void onConnectionInit();
    void onConnectionDestroy();
    void onAudioGetFormat(unsigned char* ct, unsigned short* spf,
                          bool* usingScreen, bool* isMedia, uint64_t* audioFormat);
    void onReportClientRequest(char* deviceid, char* model, char* devicename, bool* admit);

    // Read hex-encoded Ed25519 public key from PEM keyfile after raop_init2 generates it.
    // Returns 64-char lowercase hex string (32 bytes = ED25519_KEY_SIZE) or empty on failure.
    std::string readPublicKeyFromKeyfile() const;

    raop_t*           m_raop             = nullptr;
    MediaPipeline*    m_pipeline         = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    DiscoveryManager* m_discoveryManager = nullptr;
    GstElement*       m_videoAppsrc      = nullptr;
    GstElement*       m_audioAppsrc      = nullptr;
    uint64_t          m_basetime         = 0;
    bool              m_basetimeSet      = false;
    std::string       m_deviceId;
    std::string       m_keyfilePath;
    std::string       m_currentDeviceName;  // set by report_client_request
    bool              m_running          = false;
    bool              m_audioCapsSet     = false;
};

} // namespace myairshow
