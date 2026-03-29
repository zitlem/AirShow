#pragma once
#include "DecoderInfo.h"
#include <gst/gst.h>
#include <optional>
#include <functional>

namespace myairshow {

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
    // Video branch: appsrc ! h264parse ! [vaapih264dec|avdec_h264] ! videoconvert ! glupload ! qml6glsink
    // Audio branch: appsrc ! decodebin ! audioconvert ! audioresample ! autoaudiosink
    // Uses decodebin for audio to handle AAC/ALAC codec negotiation automatically.
    bool initAppsrcPipeline(void* qmlVideoItem);

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

    // Static member callback for decodebin "element-added" signal (Plan 03).
    // Declared here so it has natural access to private members via the
    // MediaPipeline* cast of the gpointer userdata — no friend needed.
    static void onElementAdded(GstBin* bin, GstElement* element, gpointer userData);
};

} // namespace myairshow
