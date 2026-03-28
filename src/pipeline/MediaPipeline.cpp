#include "pipeline/MediaPipeline.h"
#include <gst/gst.h>
#include <glib.h>

namespace myairshow {

// ── MediaPipeline ────────────────────────────────────────────────────────────

MediaPipeline::MediaPipeline() = default;

MediaPipeline::~MediaPipeline() {
    stop();
}

bool MediaPipeline::init(void* qmlVideoItem) {
    // D-05: Phase 1 uses videotestsrc + audiotestsrc to validate the pipeline.
    // D-06: Single shared pipeline — all future protocols converge on one pipeline.
    // Pipeline string concept:
    //   videotestsrc ! videoconvert ! qml6glsink
    //   audiotestsrc ! audioconvert ! autoaudiosink

    GstElement* pipeline     = gst_pipeline_new("myairshow-pipeline");
    GstElement* videoSrc     = gst_element_factory_make("videotestsrc",  "videosrc");
    GstElement* videoConvert = gst_element_factory_make("videoconvert",  "videoconvert");
    GstElement* audioSrc     = gst_element_factory_make("audiotestsrc",  "audiosrc");
    GstElement* audioConvert = gst_element_factory_make("audioconvert",  "audioconvert");
    GstElement* audioSink    = gst_element_factory_make("autoaudiosink", "audiosink");

    // Video sink selection: when a QML video item is provided use the full GL chain
    // (videoconvert ! glupload ! qml6glsink).  In headless/test mode (no item, no
    // QGuiApplication), fall back to fakesink so the audio branch can still be tested.
    const bool useGlSink = (qmlVideoItem != nullptr);
    GstElement* glUpload  = useGlSink ? gst_element_factory_make("glupload",   "glupload")  : nullptr;
    GstElement* videoSink = useGlSink ? gst_element_factory_make("qml6glsink", "videosink")
                                      : gst_element_factory_make("fakesink",   "videosink");

    if (!pipeline || !videoSrc || !videoConvert || !videoSink ||
        !audioSrc || !audioConvert || !audioSink ||
        (useGlSink && !glUpload)) {
        g_warning("MediaPipeline::init — failed to create one or more GStreamer elements");
        if (pipeline)     gst_object_unref(pipeline);
        if (videoSrc)     gst_object_unref(videoSrc);
        if (videoConvert) gst_object_unref(videoConvert);
        if (glUpload)     gst_object_unref(glUpload);
        if (videoSink)    gst_object_unref(videoSink);
        if (audioSrc)     gst_object_unref(audioSrc);
        if (audioConvert) gst_object_unref(audioConvert);
        if (audioSink)    gst_object_unref(audioSink);
        return false;
    }

    // D-04: Set the QML GstGLVideoItem on the sink BEFORE state change.
    if (useGlSink) {
        g_object_set(videoSink, "widget", qmlVideoItem, nullptr);
    }

    if (useGlSink) {
        gst_bin_add_many(GST_BIN(pipeline),
            videoSrc, videoConvert, glUpload, videoSink,
            audioSrc, audioConvert, audioSink,
            nullptr);
    } else {
        gst_bin_add_many(GST_BIN(pipeline),
            videoSrc, videoConvert, videoSink,
            audioSrc, audioConvert, audioSink,
            nullptr);
    }

    // Video branch: videotestsrc ! videoconvert [ ! glupload ! qml6glsink | ! fakesink ]
    // glupload bridges video/x-raw → video/x-raw(memory:GLMemory) for qml6glsink.
    if (useGlSink) {
        if (!gst_element_link_many(videoSrc, videoConvert, glUpload, videoSink, nullptr)) {
            g_warning("MediaPipeline::init — failed to link video branch (GL)");
            gst_object_unref(pipeline);
            return false;
        }
    } else {
        if (!gst_element_link_many(videoSrc, videoConvert, videoSink, nullptr)) {
            g_warning("MediaPipeline::init — failed to link video branch (fake)");
            gst_object_unref(pipeline);
            return false;
        }
    }
    if (!gst_element_link_many(audioSrc, audioConvert, audioSink, nullptr)) {
        g_warning("MediaPipeline::init — failed to link audio branch");
        gst_object_unref(pipeline);
        return false;
    }

    m_pipeline  = pipeline;
    m_audioSink = audioSink;

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_warning("MediaPipeline::init — GST_STATE_CHANGE_FAILURE");
        gst_object_unref(m_pipeline);
        m_pipeline  = nullptr;
        m_audioSink = nullptr;
        return false;
    }

    return true;
}

bool MediaPipeline::initDecoderPipeline() {
    // Stub — implemented in Plan 03.
    return false;
}

void MediaPipeline::setMuted(bool muted) {
    // D-08: Mute by setting volume to 0.0, not by disconnecting the audio branch.
    // This preserves A/V sync — critical per PITFALLS.md.
    m_muted = muted;
    if (m_audioSink) {
        g_object_set(m_audioSink, "volume", muted ? 0.0 : 1.0, nullptr);
    }
}

bool MediaPipeline::isMuted() const {
    return m_muted;
}

std::optional<DecoderInfo> MediaPipeline::activeDecoder() const {
    return m_activeDecoder;
}

void MediaPipeline::setDecoderSelectedCallback(DecoderSelectedCallback cb) {
    m_decoderCallback = std::move(cb);
}

void MediaPipeline::stop() {
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline  = nullptr;
        m_audioSink = nullptr;
    }
    if (m_decoderPipeline) {
        gst_element_set_state(m_decoderPipeline, GST_STATE_NULL);
        gst_object_unref(m_decoderPipeline);
        m_decoderPipeline = nullptr;
    }
}

// ── onElementAdded — static member (used by Plan 03 decoder detection) ───────
// Defined here; connected via g_signal_connect in initDecoderPipeline() (Plan 03).
// As a static member it has the same linkage as a free function but no friend
// declaration is needed — private member access goes through the userData cast.
void MediaPipeline::onElementAdded(GstBin* /*bin*/, GstElement* element, gpointer userData) {
    auto* self = static_cast<MediaPipeline*>(userData);
    if (!self) return;

    GstElementFactory* factory = gst_element_get_factory(element);
    if (!factory) return;

    const gchar* name  = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    const gchar* klass = gst_element_factory_get_klass(factory);

    if (!name || !klass) return;

    // Only care about video decoders
    if (g_strstr_len(klass, -1, "Decoder/Video") == nullptr) return;

    const bool isHardware =
        g_str_has_prefix(name, "vaapi")    ||  // Linux VAAPI (D-11)
        g_str_has_prefix(name, "nv")       ||  // NVIDIA NVDEC (D-11)
        g_str_has_prefix(name, "vtdec")    ||  // macOS VideoToolbox (D-11)
        g_str_has_prefix(name, "d3d11")    ||  // Windows D3D11 (D-11)
        g_str_has_prefix(name, "mfh264dec");   // Windows Media Foundation (D-11)

    DecoderInfo info{name, isHardware ? DecoderType::Hardware : DecoderType::Software};

    if (isHardware) {
        g_info("Hardware H.264 decoder selected: %s", name);
    } else {
        // D-12: Log a warning but do not crash or refuse to play
        g_warning("Software H.264 decoder selected: %s (hardware unavailable)", name);
    }

    self->m_activeDecoder = info;
    if (self->m_decoderCallback) {
        self->m_decoderCallback(info);
    }
}

} // namespace myairshow
