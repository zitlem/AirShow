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

    // Mute/unmute audio (D-08: set volume to 0.0 / 1.0 on autoaudiosink)
    void setMuted(bool muted);
    bool isMuted() const;

    // Returns the decoder selected by decodebin (populated after pipeline starts)
    std::optional<DecoderInfo> activeDecoder() const;

    // Callback invoked when decodebin selects a decoder
    using DecoderSelectedCallback = std::function<void(const DecoderInfo&)>;
    void setDecoderSelectedCallback(DecoderSelectedCallback cb);

    void stop();

private:
    GstElement* m_pipeline  = nullptr;
    GstElement* m_audioSink = nullptr;
    bool        m_muted     = false;
    std::optional<DecoderInfo> m_activeDecoder;
    DecoderSelectedCallback    m_decoderCallback;
};

} // namespace myairshow
