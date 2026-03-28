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
    // Per RESEARCH.md Pitfall 5: videotestsrc produces uncompressed video.
    // decodebin passes it through without adding a decoder element — the
    // element-added callback never fires for raw video.
    // Solution: videotestsrc ! x264enc ! decodebin ! videoconvert ! fakesink
    // This encodes to H.264 first, then decodes via decodebin, which WILL
    // trigger the decoder rank-based selection and fire element-added.
    //
    // x264enc lives in gstreamer1.0-plugins-ugly (Ubuntu) or plugins-bad.
    // If unavailable, log an error and apply software fallback directly (D-12).

    GstElement* pipeline     = gst_pipeline_new("decoder-detection-pipeline");
    GstElement* videoSrc     = gst_element_factory_make("videotestsrc",  "dec-videosrc");
    GstElement* encoder      = gst_element_factory_make("x264enc",       "dec-encoder");
    GstElement* decodebin    = gst_element_factory_make("decodebin",     "dec-decodebin");
    GstElement* videoConvert = gst_element_factory_make("videoconvert",  "dec-videoconvert");
    GstElement* fakeSink     = gst_element_factory_make("fakesink",      "dec-fakesink");

    if (!pipeline || !videoSrc || !decodebin || !videoConvert || !fakeSink) {
        g_warning("initDecoderPipeline — failed to create core elements");
        if (pipeline)     gst_object_unref(pipeline);
        if (videoSrc)     gst_object_unref(videoSrc);
        if (decodebin)    gst_object_unref(decodebin);
        if (videoConvert) gst_object_unref(videoConvert);
        if (fakeSink)     gst_object_unref(fakeSink);
        if (encoder)      gst_object_unref(encoder);
        return false;
    }

    if (!encoder) {
        // D-12: x264enc not installed — apply software fallback directly.
        g_warning("initDecoderPipeline — x264enc not available (install gstreamer1.0-plugins-ugly). "
                  "Decoder detection will report software fallback.");

        DecoderInfo fallback{"avdec_h264", DecoderType::Software};
        m_activeDecoder = fallback;
        g_warning("Software H.264 decoder selected: avdec_h264 (hardware unavailable)");
        if (m_decoderCallback) m_decoderCallback(fallback);

        gst_object_unref(pipeline);
        if (videoSrc)     gst_object_unref(videoSrc);
        if (decodebin)    gst_object_unref(decodebin);
        if (videoConvert) gst_object_unref(videoConvert);
        if (fakeSink)     gst_object_unref(fakeSink);
        return true;  // D-12: Not a failure — software fallback is valid
    }

    // Connect the element-added signal BEFORE setting state.
    // MediaPipeline::onElementAdded is a static member — cast to GCallback is valid
    // and it receives `this` as the gpointer userdata, giving access to all private members.
    g_signal_connect(decodebin, "element-added",
                     G_CALLBACK(MediaPipeline::onElementAdded), this);

    gst_bin_add_many(GST_BIN(pipeline),
        videoSrc, encoder, decodebin, videoConvert, fakeSink, nullptr);

    // Link videotestsrc ! x264enc ! decodebin (decodebin pads are dynamic —
    // link up to decodebin; videoConvert and fakeSink are linked via pad-added signal)
    if (!gst_element_link_many(videoSrc, encoder, decodebin, nullptr)) {
        g_warning("initDecoderPipeline — failed to link src!encoder!decodebin");
        gst_object_unref(pipeline);
        return false;
    }

    // Handle dynamic src pad from decodebin.
    // g_signal_connect is a macro that takes exactly 4 arguments; the callback
    // must be a plain function pointer, not an inline lambda (the commas in the
    // lambda body confuse the preprocessor).  Use a named static helper instead.
    struct PadAddedHelper {
        static void callback(GstElement* /*decodebin*/, GstPad* pad, gpointer data) {
            auto* payload = static_cast<std::pair<GstElement*, GstElement*>*>(data);
            GstElement* convert  = payload->first;
            GstElement* fakesink = payload->second;

            GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
            if (!sinkPad || GST_PAD_IS_LINKED(sinkPad)) {
                if (sinkPad) gst_object_unref(sinkPad);
                return;
            }
            if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                g_warning("initDecoderPipeline — pad-added link failed");
            }
            gst_object_unref(sinkPad);
            gst_element_link(convert, fakesink);
        }
    };
    auto* padData = new std::pair<GstElement*, GstElement*>(videoConvert, fakeSink);
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(PadAddedHelper::callback), padData);

    m_decoderPipeline = pipeline;

    GstStateChangeReturn ret = gst_element_set_state(m_decoderPipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_warning("initDecoderPipeline — GST_STATE_CHANGE_FAILURE");
        gst_element_set_state(m_decoderPipeline, GST_STATE_NULL);
        gst_object_unref(m_decoderPipeline);
        m_decoderPipeline = nullptr;
        return false;
    }

    return true;
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
