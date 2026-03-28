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

    // White-box accessor for tests — allows gst_element_get_state on a real pointer.
    // Returns nullptr if init() has not been called or pipeline has been stopped.
    GstElement* gstPipeline() const { return m_pipeline; }

private:
    GstElement* m_pipeline        = nullptr;
    GstElement* m_decoderPipeline = nullptr;
    GstElement* m_audioSink       = nullptr;
    bool        m_muted           = false;
    std::optional<DecoderInfo> m_activeDecoder;
    DecoderSelectedCallback    m_decoderCallback;

    // Static member callback for decodebin "element-added" signal (Plan 03).
    // Declared here so it has natural access to private members via the
    // MediaPipeline* cast of the gpointer userdata — no friend needed.
    static void onElementAdded(GstBin* bin, GstElement* element, gpointer userData);
};

} // namespace myairshow
