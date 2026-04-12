#pragma once
#include "DecoderInfo.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <optional>
#include <functional>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <QImage>

namespace airshow {

class MediaPipeline {
public:
    MediaPipeline();
    ~MediaPipeline();

    // Initialise and start the test pipeline (videotestsrc + audiotestsrc)
    bool init(void* qmlVideoItem);

    // Build a videotestsrc ! x264enc ! decodebin pipeline to exercise hardware
    // decoder detection (D-11). Added here for Plan 03; stub returns false until then.
    bool initDecoderPipeline();

    // Mute/unmute audio (D-08: set volume to 0.0 / 1.0 on autoaudiosink)
    void setMuted(bool muted);
    bool isMuted() const;

    // Returns the decoder selected by decodebin (populated after pipeline starts)
    std::optional<DecoderInfo> activeDecoder() const;

    // Callback invoked when decodebin selects a decoder
    using DecoderSelectedCallback = std::function<void(const DecoderInfo&)>;
    void setDecoderSelectedCallback(DecoderSelectedCallback cb);

    void stop();

    // Transition from PAUSED to PLAYING. Call after QML scene has rendered
    // at least one frame so the GL context is available for qml6glsink.
    void play();

    // True if init() succeeded but play() hasn't been called yet.
    bool needsPlay() const { return m_needsPlay; }

    // White-box accessor for tests — allows gst_element_get_state on a real pointer.
    // Returns nullptr if init() has not been called or pipeline has been stopped.
    GstElement* gstPipeline() const { return m_pipeline; }

    // Phase 4: appsrc-based pipeline for live protocol data (D-03)
    // Video branch: appsrc ! h264parse ! [vaapih264dec|avdec_h264] ! videoconvert ! appsink
    // Audio branch: appsrc ! decodebin ! audioconvert ! audioresample ! autoaudiosink
    // Video frames are delivered as RGBA QImages via the VideoFrameCallback set below.
    // Uses decodebin for audio to handle AAC/ALAC codec negotiation automatically.
    bool initAppsrcPipeline(void* qmlVideoItem);

    // Callback invoked from GStreamer's streaming thread when a decoded video frame
    // is ready. The QImage is RGBA format. Must be set before initAppsrcPipeline().
    using VideoFrameCallback = std::function<void(QImage)>;
    void setVideoFrameCallback(VideoFrameCallback cb);

    // Accessors for protocol handlers to push encoded buffers into the pipeline.
    // Valid after initAppsrcPipeline() returns true; null otherwise.
    GstElement* videoAppsrc() const { return m_videoAppsrc; }
    GstElement* audioAppsrc() const { return m_audioAppsrc; }

    // Set audio caps dynamically when codec type is known (avoids caps negotiation delays).
    // capsString examples:
    //   "audio/mpeg,mpegversion=4,stream-format=raw,channels=2,rate=44100"  (AAC)
    //   "audio/x-alac,channels=2,rate=44100,samplesize=16"                  (ALAC)
    void setAudioCaps(const char* capsString);

    // Phase 5: URI-based pipeline for DLNA media playback (D-04, D-05)
    // Pipeline: uridecodebin ! [video: videoconvert ! glupload ! qml6glsink]
    //                          [audio: audioconvert ! audioresample ! volume ! autoaudiosink]
    bool initUriPipeline(void* qmlVideoItem);
    void setUri(const std::string& uri);
    void playUri();
    void pauseUri();
    void stopUri();
    gint64 queryPosition() const;   // nanoseconds, or -1
    gint64 queryDuration() const;   // nanoseconds, or -1
    void seekUri(gint64 positionNs);
    void setVolume(double volume);  // 0.0-1.0; applied to uri volume element
    double getVolume() const;       // returns current volume (0.0-1.0)

    // Phase 6: WebRTC pipeline for Cast mirroring (VP8/Opus via DTLS-SRTP)
    //
    // Store the QML VideoOutput item pointer for later pipeline creation.
    // Called once at startup from ReceiverWindow/main.cpp before any Cast connection.
    // This is needed because CastSession::onWebrtc() creates the pipeline at
    // message-receive time but has no direct access to the QML scene graph item.
    void setQmlVideoItem(void* qmlVideoItem);

    // Creates a webrtcbin element in a new pipeline with VP8 video and Opus audio
    // decode chains, using the stored m_qmlVideoItem set via setQmlVideoItem().
    // Returns false if m_qmlVideoItem is null (setQmlVideoItem not called).
    bool initWebrtcPipeline();

    // Feed a remote SDP offer to webrtcbin. Returns false if webrtcbin rejects it.
    // sdpOffer: standard SDP string (translated from Cast OFFER JSON by CastSession).
    bool setRemoteOffer(const std::string& sdpOffer);

    // Get the local SDP answer from webrtcbin after setRemoteOffer succeeds.
    // Returns empty string on failure. CastSession translates this back to Cast ANSWER JSON.
    std::string getLocalAnswer();

    // Set Cast AES-CTR decryption keys for a given SSRC stream.
    // Called by CastSession after parsing OFFER JSON aesKey/aesIvMask fields.
    // Keys are stored and conditionally applied during pad-added decryption.
    void setCastDecryptionKeys(uint32_t ssrc,
                               const std::string& aesKeyHex,
                               const std::string& aesIvMaskHex);

    // Get the webrtcbin element (for external ICE candidate injection if needed).
    GstElement* webrtcbin() const { return m_webrtcbin; }

    // ICE candidate callback — invoked on GStreamer thread when webrtcbin
    // gathers a local ICE candidate. CastSession registers this to relay
    // candidates back to the Cast sender.
    using IceCandidateCallback = std::function<void(unsigned int mlineIndex,
                                                     const std::string& candidate)>;
    void setIceCandidateCallback(IceCandidateCallback cb);

    // Query the local UDP port that webrtcbin has bound to for RTP/DTLS.
    // Returns 0 if not yet known.
    uint16_t webrtcLocalPort() const;

    // Phase 8: Miracast MPEG-TS/RTP receive pipeline (D-09, D-10).
    //
    // Pipeline: udpsrc(port=udpPort) ! rtpmp2tdepay ! tsparse ! tsdemux
    //   [video: queue ! h264parse ! vaapidecodebin/avdec_h264 ! videoconvert ! glupload ! qml6glsink]
    //   [audio: queue ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink]
    //
    // Uses dynamic pads from tsdemux (same pattern as uridecodebin in initUriPipeline).
    // If qmlVideoItem is nullptr, uses fakesink for video (headless test mode).
    // Pipeline starts in GST_STATE_PAUSED; MiracastHandler calls play() when PLAY received.
    bool initMiracastPipeline(void* qmlVideoItem, int udpPort);

    // Stop and clean up the Miracast pipeline. Safe to call if not started.
    void stopMiracast();

    // White-box accessor for tests — allows gst_element_get_state on the miracast pipeline.
    GstElement* miracastPipeline() const { return m_miracastPipeline; }

private:
    GstElement* m_pipeline        = nullptr;
    GstElement* m_decoderPipeline = nullptr;
    GstElement* m_audioSink       = nullptr;
    GstElement* m_videoAppsrc     = nullptr;
    GstElement* m_audioAppsrc     = nullptr;
    bool        m_muted           = false;
    bool        m_needsPlay       = false;
    std::optional<DecoderInfo> m_activeDecoder;
    DecoderSelectedCallback    m_decoderCallback;

    // Phase 5: URI-based pipeline members (separate from appsrc pipeline)
    GstElement* m_uriPipeline  = nullptr;  // separate pipeline for URI mode
    GstElement* m_uriDecodebin = nullptr;  // kept for uri property access
    GstElement* m_uriAudioSink = nullptr;  // for mute/volume control in URI mode
    GstElement* m_uriVolume    = nullptr;  // volume element for SetVolume

    // Phase 6: WebRTC pipeline members (separate pipeline for Cast mirroring)
    void*       m_qmlVideoItem   = nullptr;  // stored by setQmlVideoItem()
    GstElement* m_webrtcPipeline = nullptr;  // pipeline for webrtcbin
    GstElement* m_webrtcbin      = nullptr;  // the webrtcbin element

    // Cast AES-CTR decryption keys per SSRC (conditional: only applied when key is present)
    struct CastCryptoKeys {
        std::vector<uint8_t> aesKey;     // 16 bytes (128-bit)
        std::vector<uint8_t> aesIvMask;  // 16 bytes (128-bit)
    };
    std::map<uint32_t, CastCryptoKeys> m_castCryptoKeys;

    // Stored SDP answer from webrtcbin after setRemoteOffer()
    std::string m_localAnswerSdp;

    // ICE candidate callback for external notification
    IceCandidateCallback m_iceCandidateCallback;

    // Local UDP port discovered from ICE candidates
    uint16_t m_webrtcLocalPort = 0;

    // Phase 8: Miracast MPEG-TS/RTP pipeline (separate pipeline, same pattern as m_uriPipeline)
    GstElement* m_miracastPipeline = nullptr;

    // Static member callback for decodebin "element-added" signal (Plan 03).
    // Declared here so it has natural access to private members via the
    // MediaPipeline* cast of the gpointer userdata — no friend needed.
    static void onElementAdded(GstBin* bin, GstElement* element, gpointer userData);

    // Static appsink new-sample callback — called from GStreamer's streaming thread.
    // Must match GstAppSinkCallbacks::new_sample signature (GstAppSink*, not GstElement*).
    static GstFlowReturn onNewVideoSample(GstAppSink* sink, gpointer userData);

    VideoFrameCallback m_videoFrameCallback;
};

} // namespace airshow
