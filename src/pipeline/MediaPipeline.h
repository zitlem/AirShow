#pragma once
#include "DecoderInfo.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <optional>
#include <functional>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <QImage>

namespace airshow {

class MediaPipeline {
public:
    MediaPipeline();
    ~MediaPipeline();

    // Initialise and start the test pipeline (videotestsrc + audiotestsrc).
    // Video goes to the VideoFrameCallback; qmlVideoItem is unused (kept for API
    // compatibility with the earlier qml6glsink design), as in the other init*() calls.
    bool init(void* qmlVideoItem);

    // Build a videotestsrc ! x264enc ! decodebin pipeline to exercise hardware
    // decoder detection (D-11). Added here for Plan 03; stub returns false until then.
    bool initDecoderPipeline();

    // Mute/unmute audio (D-08: set volume to 0.0 / 1.0 on autoaudiosink)
    void setMuted(bool muted);
    bool isMuted() const;

    // Returns the decoder selected by decodebin (populated after pipeline starts)
    std::optional<DecoderInfo> activeDecoder() const;

    // Callback invoked when decodebin selects a decoder — on GStreamer's
    // streaming thread, possibly more than once (fallback between decoders).
    using DecoderSelectedCallback = std::function<void(const DecoderInfo&)>;
    void setDecoderSelectedCallback(DecoderSelectedCallback cb);

    void stop();

    // Transition the active pipelines to PLAYING.
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
    // Pipeline: uridecodebin ! [video: videoconvert ! appsink → VideoFrameCallback]
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

    // Store the QML video item pointer (kept for the protocol handlers' startup
    // wiring; all video now goes through the VideoFrameCallback).
    void setQmlVideoItem(void* qmlVideoItem);

    // Google Cast Streaming playback. CastStreamingReceiver does the network side
    // and pushes complete, decrypted encoded frames here.
    // videoCaps / audioCaps are GStreamer caps strings for the selected codecs
    // (e.g. "video/x-vp8", "audio/x-opus,..."); pass "" to omit a branch.
    bool initCastPipeline(const std::string& videoCaps, const std::string& audioCaps);
    void pushCastVideoFrame(const uint8_t* data, size_t size, bool keyFrame);
    void pushCastAudioFrame(const uint8_t* data, size_t size);
    void stopCast();
    GstElement* castPipeline() const { return m_castPipeline; }

    // Phase 8: Miracast MPEG-TS/RTP receive pipeline (D-09, D-10).
    //
    // Pipeline: udpsrc(port=udpPort) ! rtpmp2tdepay ! tsparse ! tsdemux
    //   [video: queue ! h264parse ! vaapidecodebin/avdec_h264 ! videoconvert ! appsink]
    //   [audio: queue ! decodebin (AAC or LPCM) ! audioconvert ! audioresample ! autoaudiosink]
    //
    // Uses dynamic pads from tsdemux (same pattern as uridecodebin in initUriPipeline).
    // Video goes to the VideoFrameCallback (fakesink if none is set — headless tests).
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
    // Written from GStreamer's streaming thread (decodebin element-added), read
    // from the caller's thread; decodebin may try several decoders in turn.
    mutable std::mutex         m_decoderMutex;
    std::optional<DecoderInfo> m_activeDecoder;
    DecoderSelectedCallback    m_decoderCallback;

    // Phase 5: URI-based pipeline members (separate from appsrc pipeline)
    GstElement* m_uriPipeline  = nullptr;  // separate pipeline for URI mode
    GstElement* m_uriDecodebin = nullptr;  // kept for uri property access
    GstElement* m_uriAudioSink = nullptr;  // for mute/volume control in URI mode
    GstElement* m_uriVolume    = nullptr;  // volume element for SetVolume

    void*       m_qmlVideoItem   = nullptr;  // stored by setQmlVideoItem()

    // Google Cast Streaming playback pipeline (separate from the AirPlay appsrc pipeline)
    GstElement* m_castPipeline  = nullptr;
    GstElement* m_castVideoSrc  = nullptr;
    GstElement* m_castAudioSrc  = nullptr;
    GstElement* m_castAudioSink = nullptr;

    // Phase 8: Miracast MPEG-TS/RTP pipeline (separate pipeline, same pattern as m_uriPipeline)
    GstElement* m_miracastPipeline = nullptr;

    // Static member callback for decodebin "element-added" signal (Plan 03).
    // Declared here so it has natural access to private members via the
    // MediaPipeline* cast of the gpointer userdata — no friend needed.
    static void onElementAdded(GstBin* bin, GstElement* element, gpointer userData);

    // Static appsink new-sample callback — called from GStreamer's streaming thread.
    // Must match GstAppSinkCallbacks::new_sample signature (GstAppSink*, not GstElement*).
    static GstFlowReturn onNewVideoSample(GstAppSink* sink, gpointer userData);

    // Video output shared by every display pipeline: an appsink that delivers RGBA
    // QImages to m_videoFrameCallback (→ VideoFrameSink in QML), or a fakesink when
    // no callback is registered (headless tests). Callers put videoconvert in front.
    // realtime: mirroring streams show frames as soon as they decode (no clock sync,
    // no preroll); false for file/URI playback, which must play at its own pace.
    GstElement* createVideoSink(const char* name, bool realtime);

    VideoFrameCallback m_videoFrameCallback;
};

} // namespace airshow
