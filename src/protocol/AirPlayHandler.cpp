#include "protocol/AirPlayHandler.h"

// UxPlay RAOP headers (defines raop_callbacks_t, video_decode_struct, audio_decode_struct)
// Must be included before GStreamer to avoid type redefinition conflicts.
#include <raop.h>
#include <dnssd.h>
#include <stream.h>

// GStreamer
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

// Project headers
#include "pipeline/MediaPipeline.h"
#include "ui/ConnectionBridge.h"
#include "discovery/DiscoveryManager.h"
#include "security/SecurityManager.h"

// Qt
#include <QMetaObject>

// C++ stdlib
#include <iomanip>
#include <sstream>

// OpenSSL — for reading PEM private key and extracting raw public key bytes
#include <openssl/pem.h>
#include <openssl/evp.h>

// GLib for g_warning / g_message
#include <glib.h>

// ── File-scope C trampolines ──────────────────────────────────────────────────
//
// raop_callbacks_t requires plain C function pointers with exact UxPlay signatures.
// These file-scope functions bridge the C callback API to the C++ instance methods.
// They are defined here (not as static class members) to avoid the need to include
// UxPlay headers (stream.h etc.) in the public AirPlayHandler.h header.
//
static void raop_cb_video_process(void* cls, raop_ntp_t* /*ntp*/, video_decode_struct* data) {
    static_cast<airshow::AirPlayHandler*>(cls)->onVideoFrame(data);
}

static void raop_cb_audio_process(void* cls, raop_ntp_t* /*ntp*/, audio_decode_struct* data) {
    static_cast<airshow::AirPlayHandler*>(cls)->onAudioFrame(data);
}

static void raop_cb_conn_init(void* cls) {
    static_cast<airshow::AirPlayHandler*>(cls)->onConnectionInit();
}

static void raop_cb_conn_destroy(void* cls) {
    static_cast<airshow::AirPlayHandler*>(cls)->onConnectionDestroy();
}

static void raop_cb_conn_teardown(void* cls, bool* teardown_96, bool* teardown_110) {
    static_cast<airshow::AirPlayHandler*>(cls)->onConnTeardown(teardown_96, teardown_110);
}

static void raop_cb_audio_get_format(void* cls, unsigned char* ct, unsigned short* spf,
                                     bool* usingScreen, bool* isMedia, uint64_t* audioFormat) {
    static_cast<airshow::AirPlayHandler*>(cls)->onAudioGetFormat(ct, spf, usingScreen, isMedia, audioFormat);
}

static void raop_cb_report_client_request(void* cls, char* deviceid, char* model,
                                          char* name, bool* admit) {
    static_cast<airshow::AirPlayHandler*>(cls)->onReportClientRequest(deviceid, model, name, admit);
}

static void raop_cb_display_pin(void* cls, char* pin) {
    static_cast<airshow::AirPlayHandler*>(cls)->onDisplayPin(pin ? pin : "");
}

// UxPlay calls these callbacks unconditionally (no null guard in UxPlay source).
// Without them the function pointers are null → assert / SIGSEGV.

static double raop_cb_audio_set_client_volume(void* /*cls*/) {
    // Called by raop_handler_info on every /info probe. Return -144 dB (muted/unset).
    return -144.0;
}

static int raop_cb_video_set_codec(void* /*cls*/, video_codec_t /*codec*/) {
    // Called when the mirror RTP thread detects a codec change (H.264/H.265).
    // Return 0 = accepted. AirShow uses a fixed GStreamer pipeline so no action needed.
    return 0;
}

static void raop_cb_video_reset(void* /*cls*/, reset_type_t /*reset_type*/) {
    // Called on RTP shutdown / connection reset. Pipeline teardown is handled
    // by onConnectionDestroy(); this stub satisfies UxPlay's unconditional call.
}

static void raop_cb_conn_feedback(void* /*cls*/) {
    // Called on each client heartbeat "feedback" message. No-op for AirShow.
}

// ─────────────────────────────────────────────────────────────────────────────

namespace airshow {

// ── Constructor / Destructor ──────────────────────────────────────────────────

AirPlayHandler::AirPlayHandler(ConnectionBridge* connectionBridge,
                               DiscoveryManager* discoveryManager,
                               const std::string& deviceId,
                               const std::string& keyfilePath)
    : m_connectionBridge(connectionBridge)
    , m_discoveryManager(discoveryManager)
    , m_deviceId(deviceId)
    , m_keyfilePath(keyfilePath) {}

AirPlayHandler::~AirPlayHandler() {
    stop();
}

// ── setMediaPipeline ──────────────────────────────────────────────────────────

void AirPlayHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
    // Do NOT call initAppsrcPipeline here — the QML video item is not yet available
    // (sceneGraphInitialized hasn't fired). ReceiverWindow calls initAppsrcPipeline(videoItem)
    // after the GL context is ready. m_videoAppsrc/m_audioAppsrc are fetched lazily in
    // onVideoFrame/onAudioFrame via m_pipeline->videoAppsrc() / audioAppsrc().
}

// ── setSecurityManager() ─────────────────────────────────────────────────────

void AirPlayHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
}

// ── start() ──────────────────────────────────────────────────────────────────

bool AirPlayHandler::start() {
    if (m_running) {
        return true;
    }

    // 1. Wire all RAOP callbacks using file-scope trampoline functions
    raop_callbacks_t callbacks{};
    callbacks.cls                   = this;
    callbacks.video_process         = raop_cb_video_process;
    callbacks.audio_process         = raop_cb_audio_process;
    callbacks.conn_init             = raop_cb_conn_init;
    callbacks.conn_destroy          = raop_cb_conn_destroy;
    callbacks.conn_teardown         = raop_cb_conn_teardown;
    callbacks.audio_get_format      = raop_cb_audio_get_format;
    callbacks.report_client_request       = raop_cb_report_client_request;
    callbacks.audio_set_client_volume     = raop_cb_audio_set_client_volume;
    callbacks.video_set_codec             = raop_cb_video_set_codec;
    callbacks.video_reset                 = raop_cb_video_reset;
    callbacks.conn_feedback               = raop_cb_conn_feedback;
    // PIN pairing (D-05, D-06): set display_pin trampoline if PIN is enabled
    if (m_securityManager && m_securityManager->isPinEnabled()) {
        callbacks.display_pin = raop_cb_display_pin;
    }

    // 2. Initialise RAOP server
    m_raop = raop_init(&callbacks);
    if (!m_raop) {
        g_warning("AirPlayHandler::start — raop_init() returned nullptr");
        return false;
    }

    // 2b. Set logger callback — UxPlay's logger asserts if no callback is set,
    //     causing a crash on the first HTTP request (e.g., /info from iOS/macOS).
    raop_set_log_callback(m_raop, [](void* /*cls*/, int /*level*/, const char* msg) {
        // Print all UxPlay messages to stderr so they appear in the log.
        // LOGGER_INFO=6, LOGGER_WARNING=4, LOGGER_ERR=3 — use g_message (always visible).
        g_message("UxPlay: %s", msg);
    }, nullptr);
    raop_set_log_level(m_raop, 6);  // LOGGER_INFO — captures conn lifecycle messages

    // 2c. Initialize UxPlay's dnssd object — the /info handler reads features and
    //     TXT records from raop->dnssd. We don't use UxPlay's mDNS (our DiscoveryManager
    //     handles that), but dnssd_t must exist for the HTTP handler to work.
    {
        int dnssd_err = 0;
        dnssd_t* dnssd = dnssd_init(m_deviceId.c_str(),
                                     static_cast<int>(m_deviceId.size()),
                                     m_deviceId.c_str(),
                                     static_cast<int>(m_deviceId.size()),
                                     &dnssd_err, 0);
        if (!dnssd) {
            g_warning("AirPlayHandler::start — dnssd_init failed (err=%d)", dnssd_err);
        } else {
            raop_set_dnssd(m_raop, dnssd);
        }
    }

    // 2d. Enable HLS support — without this, UxPlay drops all requests that lack
    //     a CSeq header (including macOS's initial /info probe), preventing the
    //     device from appearing in Screen Mirroring UI.
    raop_set_plist(m_raop, "hls", 1);

    // 3. Generate/load Ed25519 keypair and write PEM keyfile.
    //    nohold=0 → normal (non-hold) mode.
    //    We do NOT call raop_set_dnssd() — AirShow's DiscoveryManager already advertises
    //    _airplay._tcp and _raop._tcp (Pitfall 7: prevent duplicate mDNS entries).
    int keyfileResult = raop_init2(m_raop, 0, m_deviceId.c_str(), m_keyfilePath.c_str());
    if (keyfileResult < 0) {
        g_warning("AirPlayHandler::start — raop_init2() failed (result=%d)", keyfileResult);
        raop_destroy(m_raop);
        m_raop = nullptr;
        return false;
    }

    // 3b. Enable PIN pairing if configured (D-05, D-06, D-07).
    //     MUST be called after raop_init2 and before raop_start_httpd (Pitfall 2).
    if (m_securityManager && m_securityManager->isPinEnabled()) {
        int pinValue = m_securityManager->pin().toInt();
        raop_set_plist(m_raop, "pin", pinValue);
    }

    // 4. Read pk from PEM keyfile and update TXT records (Pitfall 1)
    std::string pk = readPublicKeyFromKeyfile();
    if (pk.empty()) {
        g_warning("AirPlayHandler::start — readPublicKeyFromKeyfile() returned empty — iOS will not connect");
    } else if (pk.size() != 64) {
        g_warning("AirPlayHandler::start — pk has unexpected length %zu (expected 64)", pk.size());
    } else {
        if (m_discoveryManager) {
            m_discoveryManager->updateTxtRecord("_airplay._tcp", "pk", pk);
            m_discoveryManager->updateTxtRecord("_raop._tcp",    "pk", pk);
        }
    }

    // 5. Start HTTPD on port 7000 (required by AirPlay protocol; Pitfall 2)
    unsigned short port = 7000;
    int httpResult = raop_start_httpd(m_raop, &port);
    if (httpResult < 0) {
        g_warning("AirPlayHandler::start — raop_start_httpd() failed (result=%d)", httpResult);
        raop_destroy(m_raop);
        m_raop = nullptr;
        return false;
    }
    if (port != 7000) {
        g_warning("AirPlayHandler::start — RAOP server bound to port %u instead of 7000 — iOS may not connect",
                  static_cast<unsigned>(port));
    }

    m_running = true;
    return true;
}

// ── stop() ───────────────────────────────────────────────────────────────────

void AirPlayHandler::stop() {
    if (m_raop) {
        raop_stop_httpd(m_raop);
        raop_destroy(m_raop);
        m_raop = nullptr;
    }
    m_running       = false;
    m_videoAppsrc   = nullptr;
    m_audioAppsrc   = nullptr;
    m_audioCapsSet  = false;
    m_currentDeviceName.clear();
}

// ── Instance callback handlers ────────────────────────────────────────────────

void AirPlayHandler::onVideoFrame(void* rawData) {
    auto* data       = static_cast<video_decode_struct*>(rawData);
    // Fetch appsrc lazily — initAppsrcPipeline is called after sceneGraphInitialized,
    // which happens after setMediaPipeline. Cache it once available.
    if (!m_videoAppsrc && m_pipeline) {
        m_videoAppsrc = m_pipeline->videoAppsrc();
    }
    auto* videoAppsrc = static_cast<GstElement*>(m_videoAppsrc);

    if (!videoAppsrc || !data || data->data_len <= 0) {
        return;
    }

    GstBuffer* buf = gst_buffer_new_allocate(nullptr, static_cast<gsize>(data->data_len), nullptr);
    if (!buf) {
        g_warning("AirPlayHandler::onVideoFrame — gst_buffer_new_allocate failed (len=%d)", data->data_len);
        return;
    }
    gst_buffer_fill(buf, 0, data->data, static_cast<gsize>(data->data_len));
    // PTS is left as GST_CLOCK_TIME_NONE — appsrc do-timestamp=TRUE assigns the
    // pipeline running_time automatically, which is correct for live screen mirroring.

    static int s_videoFrameCount = 0;
    ++s_videoFrameCount;
    // Log every frame > 1000 bytes (scene change) and every 60th frame for heartbeat
    if (data->data_len > 1000 || s_videoFrameCount <= 5 || s_videoFrameCount % 60 == 0) {
        g_message("AirPlayHandler::onVideoFrame — frame #%d len=%d",
                  s_videoFrameCount, data->data_len);
    }

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(videoAppsrc), buf);
    if (ret != GST_FLOW_OK) {
        g_warning("AirPlayHandler::onVideoFrame — push_buffer failed frame #%d ret=%d (%s)",
                  s_videoFrameCount, ret, gst_flow_get_name(ret));
    }
}

void AirPlayHandler::onAudioFrame(void* rawData) {
    auto* data        = static_cast<audio_decode_struct*>(rawData);
    // Fetch appsrc lazily — same deferred-init pattern as onVideoFrame.
    if (!m_audioAppsrc && m_pipeline) {
        m_audioAppsrc = m_pipeline->audioAppsrc();
    }
    auto* audioAppsrc = static_cast<GstElement*>(m_audioAppsrc);

    if (!audioAppsrc || !data || data->data_len <= 0) {
        return;
    }

    GstBuffer* buf = gst_buffer_new_allocate(nullptr, static_cast<gsize>(data->data_len), nullptr);
    if (!buf) {
        g_warning("AirPlayHandler::onAudioFrame — gst_buffer_new_allocate failed (len=%d)", data->data_len);
        return;
    }
    gst_buffer_fill(buf, 0, data->data, static_cast<gsize>(data->data_len));
    // PTS left as GST_CLOCK_TIME_NONE — appsrc do-timestamp=TRUE handles timing.

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(audioAppsrc), buf);
    if (ret != GST_FLOW_OK) {
        g_warning("AirPlayHandler::onAudioFrame — gst_app_src_push_buffer failed (ret=%d)", ret);
    }
}

void AirPlayHandler::onConnTeardown(bool* teardown_96, bool* teardown_110) {
    // Allow full teardown for both RTSP 96 and 110 streams
    if (teardown_96)  *teardown_96  = true;
    if (teardown_110) *teardown_110 = true;
}

void AirPlayHandler::onAudioGetFormat(unsigned char* ct, unsigned short* /*spf*/,
                                      bool* /*usingScreen*/, bool* /*isMedia*/,
                                      uint64_t* /*audioFormat*/) {
    // Only set caps once per session to avoid downstream caps renegotiation (Pitfall 6)
    if (m_audioCapsSet || !m_pipeline) {
        return;
    }
    if (!ct) {
        return;
    }

    // ct byte: 0x20 = ALAC, 0x80+ = AAC-ELD
    if (*ct == 0x20) {
        m_pipeline->setAudioCaps("audio/x-alac,channels=2,rate=44100,samplesize=16");
    } else {
        // AAC-ELD or other AAC variant
        m_pipeline->setAudioCaps("audio/mpeg,mpegversion=4,stream-format=raw,channels=2,rate=44100");
    }
    m_audioCapsSet = true;
}

void AirPlayHandler::onReportClientRequest(char* deviceid, char* /*model*/,
                                           char* devicename, bool* admit) {
    m_currentDeviceName = (devicename && *devicename) ? std::string(devicename) : "Unknown";

    // Phase 7 (SEC-01): Check with SecurityManager before admitting connection.
    // Runs on UxPlay's RAOP thread — use synchronous checkConnection (QSemaphore blocking),
    // NOT checkConnectionAsync (which is only for the Qt main thread).
    if (admit) {
        if (m_securityManager) {
            QString name = QString::fromStdString(m_currentDeviceName);
            QString id   = (deviceid && *deviceid) ? QString::fromUtf8(deviceid) : name;
            *admit = m_securityManager->checkConnection(name, QStringLiteral("AirPlay"), id);
        } else {
            // Backward compatible: no SecurityManager → admit all (D-08)
            *admit = true;
        }
    }
}

void AirPlayHandler::onConnectionInit() {
    g_message("AirPlayHandler::onConnectionInit — device='%s' bridge=%p",
              m_currentDeviceName.c_str(), static_cast<void*>(m_connectionBridge));
    // Update HUD thread-safely via Qt queued connection (D-10)
    if (m_connectionBridge) {
        QString deviceName = QString::fromStdString(m_currentDeviceName);
        QMetaObject::invokeMethod(m_connectionBridge, [this, deviceName]() {
            g_message("AirPlayHandler::onConnectionInit — setConnected(true) firing on main thread");
            m_connectionBridge->setConnected(true, deviceName, QStringLiteral("AirPlay"));
        }, Qt::QueuedConnection);
    }
}

void AirPlayHandler::onConnectionDestroy() {
    g_message("AirPlayHandler::onConnectionDestroy — bridge=%p",
              static_cast<void*>(m_connectionBridge));
    // Update HUD thread-safely (D-10, D-11)
    if (m_connectionBridge) {
        QMetaObject::invokeMethod(m_connectionBridge, [this]() {
            g_message("AirPlayHandler::onConnectionDestroy — setConnected(false) firing on main thread");
            m_connectionBridge->setConnected(false);
        }, Qt::QueuedConnection);
    }
    // Reset per-session state so next connection starts clean
    m_videoAppsrc  = nullptr;
    m_audioAppsrc  = nullptr;
    m_audioCapsSet = false;
}

// ── readPublicKeyFromKeyfile() ────────────────────────────────────────────────
//
// [Deviation from plan spec] UxPlay writes an Ed25519 private key in PEM format
// via PEM_write_bio_PrivateKey (see vendor/uxplay/lib/crypto.c:ed25519_key_generate).
// The plan spec described a 64-byte binary file — that is incorrect; the actual file
// is a PEM-encoded private key.
//
// Correct approach: read the PEM private key via OpenSSL EVP_PKEY API, then call
// EVP_PKEY_get_raw_public_key to retrieve the raw 32-byte Ed25519 public key,
// then hex-encode to produce the 64-char lowercase hex string for the mDNS TXT record.
//
std::string AirPlayHandler::readPublicKeyFromKeyfile() const {
    if (m_keyfilePath.empty()) {
        return {};
    }

    FILE* fp = fopen(m_keyfilePath.c_str(), "r");
    if (!fp) {
        g_warning("AirPlayHandler::readPublicKeyFromKeyfile — cannot open keyfile: %s",
                  m_keyfilePath.c_str());
        return {};
    }

    EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!pkey) {
        g_warning("AirPlayHandler::readPublicKeyFromKeyfile — PEM_read_PrivateKey failed for: %s",
                  m_keyfilePath.c_str());
        return {};
    }

    // Extract raw 32-byte Ed25519 public key
    constexpr size_t ED25519_PK_SIZE = 32;
    unsigned char pubkey[ED25519_PK_SIZE];
    size_t pubkeyLen = ED25519_PK_SIZE;

    int ok = EVP_PKEY_get_raw_public_key(pkey, pubkey, &pubkeyLen);
    EVP_PKEY_free(pkey);

    if (!ok || pubkeyLen != ED25519_PK_SIZE) {
        g_warning("AirPlayHandler::readPublicKeyFromKeyfile — EVP_PKEY_get_raw_public_key failed "
                  "(ok=%d, len=%zu)", ok, pubkeyLen);
        return {};
    }

    // Hex-encode: 32 bytes → 64 lowercase hex chars
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < ED25519_PK_SIZE; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(pubkey[i]);
    }
    std::string hexPk = oss.str();

    // Sanity check: must be 64 chars, all [0-9a-f]
    if (hexPk.size() != 64) {
        g_warning("AirPlayHandler::readPublicKeyFromKeyfile — unexpected pk length %zu (expected 64)",
                  hexPk.size());
        return {};
    }

    return hexPk;
}

// ── onDisplayPin() ───────────────────────────────────────────────────────────
//
// Invoked by raop_cb_display_pin when UxPlay activates PIN pairing mode.
// The PIN to display is already stored in AppSettings (via SecurityManager::pin()).
// IdleScreen.qml shows appSettings.pin automatically when appSettings.pinEnabled is true.
// This callback is a no-op in AirShow's architecture — the PIN display is driven
// by QML property bindings, not by runtime UxPlay notifications.
//
void AirPlayHandler::onDisplayPin(const std::string& pin) {
    // Log the PIN for debugging; display is handled by IdleScreen.qml binding to
    // appSettings.pin and appSettings.pinEnabled (Phase 7 Plan 03 / D-05, D-14).
    if (!pin.empty()) {
        g_debug("AirPlayHandler::onDisplayPin — UxPlay PIN pairing code: %s", pin.c_str());
    }
}

} // namespace airshow
