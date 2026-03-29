#include "pipeline/MediaPipeline.h"
#include <gst/gst.h>
#include <glib.h>
#include <tuple>

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
    // Phase 5: Also mute the URI pipeline volume element if active
    if (m_uriVolume) {
        g_object_set(m_uriVolume, "volume", muted ? 0.0 : 1.0, nullptr);
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
        m_pipeline    = nullptr;
        m_audioSink   = nullptr;
        m_videoAppsrc = nullptr;
        m_audioAppsrc = nullptr;
    }
    if (m_decoderPipeline) {
        gst_element_set_state(m_decoderPipeline, GST_STATE_NULL);
        gst_object_unref(m_decoderPipeline);
        m_decoderPipeline = nullptr;
    }
    // Phase 5: clean up URI pipeline
    if (m_uriPipeline) {
        gst_element_set_state(m_uriPipeline, GST_STATE_NULL);
        gst_object_unref(m_uriPipeline);
        m_uriPipeline  = nullptr;
        m_uriDecodebin = nullptr;
        m_uriAudioSink = nullptr;
        m_uriVolume    = nullptr;
    }
}

bool MediaPipeline::initAppsrcPipeline(void* qmlVideoItem) {
    // Phase 4 appsrc-based pipeline for receiving live AirPlay A/V frames (D-03).
    //
    // Video branch: appsrc(video_appsrc) ! h264parse ! [vaapih264dec|avdec_h264]
    //               ! videoconvert ! glupload ! qml6glsink  (or fakesink in headless mode)
    //
    // Audio branch: appsrc(audio_appsrc) ! decodebin ! audioconvert ! audioresample ! autoaudiosink
    //   decodebin is used for audio so AAC and ALAC are both handled via codec negotiation
    //   without requiring separate code paths.
    //
    // The pipeline is started in GST_STATE_PAUSED; AirPlayHandler transitions it to
    // GST_STATE_PLAYING when the first media frame arrives, allowing A/V sync establishment.

    GstElement* pipeline      = gst_pipeline_new("myairshow-pipeline");
    GstElement* videoAppsrc   = gst_element_factory_make("appsrc",       "video_appsrc");
    GstElement* h264parse     = gst_element_factory_make("h264parse",    "h264parse");
    GstElement* videoConvert  = gst_element_factory_make("videoconvert", "videoconvert");
    GstElement* audioAppsrc   = gst_element_factory_make("appsrc",       "audio_appsrc");
    GstElement* audioDecode   = gst_element_factory_make("decodebin",    "audiodecode");
    GstElement* audioConvert  = gst_element_factory_make("audioconvert", "audioconvert");
    GstElement* audioResample = gst_element_factory_make("audioresample","audioresample");
    GstElement* audioSink     = gst_element_factory_make("autoaudiosink","audiosink");

    // Video decoder: try hardware (vaapih264dec) first, fall back to avdec_h264 (D-12)
    GstElement* videoDecode = gst_element_factory_make("vaapih264dec", "videodecode");
    if (!videoDecode) {
        g_message("MediaPipeline::initAppsrcPipeline — vaapih264dec not available, falling back to avdec_h264");
        videoDecode = gst_element_factory_make("avdec_h264", "videodecode");
    }

    // Video sink selection: qml6glsink with glupload in GL mode, fakesink in headless mode
    const bool useGlSink = (qmlVideoItem != nullptr);
    GstElement* glUpload  = useGlSink ? gst_element_factory_make("glupload",   "glupload")  : nullptr;
    GstElement* videoSink = useGlSink ? gst_element_factory_make("qml6glsink", "videosink")
                                      : gst_element_factory_make("fakesink",   "videosink");

    // Verify all mandatory elements were created
    if (!pipeline || !videoAppsrc || !h264parse || !videoDecode || !videoConvert || !videoSink ||
        !audioAppsrc || !audioDecode || !audioConvert || !audioResample || !audioSink ||
        (useGlSink && !glUpload)) {
        g_warning("MediaPipeline::initAppsrcPipeline — failed to create one or more GStreamer elements");
        if (pipeline)      gst_object_unref(pipeline);
        if (videoAppsrc)   gst_object_unref(videoAppsrc);
        if (h264parse)     gst_object_unref(h264parse);
        if (videoDecode)   gst_object_unref(videoDecode);
        if (videoConvert)  gst_object_unref(videoConvert);
        if (glUpload)      gst_object_unref(glUpload);
        if (videoSink)     gst_object_unref(videoSink);
        if (audioAppsrc)   gst_object_unref(audioAppsrc);
        if (audioDecode)   gst_object_unref(audioDecode);
        if (audioConvert)  gst_object_unref(audioConvert);
        if (audioResample) gst_object_unref(audioResample);
        if (audioSink)     gst_object_unref(audioSink);
        return false;
    }

    // Configure video appsrc: stream-type=0 (stream), format=TIME, is-live=TRUE
    // caps set to byte-stream H.264 NAL units (AirPlay sends in byte-stream format)
    g_object_set(videoAppsrc,
        "stream-type", 0,
        "format",      GST_FORMAT_TIME,
        "is-live",     TRUE,
        nullptr);
    GstCaps* videoCaps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=nal");
    g_object_set(videoAppsrc, "caps", videoCaps, nullptr);
    gst_caps_unref(videoCaps);

    // Configure audio appsrc: stream-type=0, format=TIME, is-live=TRUE
    // caps will be set later via setAudioCaps() when the codec type is known
    g_object_set(audioAppsrc,
        "stream-type", 0,
        "format",      GST_FORMAT_TIME,
        "is-live",     TRUE,
        nullptr);

    // Set the QML video item on the sink BEFORE state change (D-04)
    if (useGlSink) {
        g_object_set(videoSink, "widget", qmlVideoItem, nullptr);
    }

    // Add all elements to the pipeline
    if (useGlSink) {
        gst_bin_add_many(GST_BIN(pipeline),
            videoAppsrc, h264parse, videoDecode, videoConvert, glUpload, videoSink,
            audioAppsrc, audioDecode, audioConvert, audioResample, audioSink,
            nullptr);
    } else {
        gst_bin_add_many(GST_BIN(pipeline),
            videoAppsrc, h264parse, videoDecode, videoConvert, videoSink,
            audioAppsrc, audioDecode, audioConvert, audioResample, audioSink,
            nullptr);
    }

    // Link video branch: appsrc ! h264parse ! videodecode ! videoconvert [! glupload] ! videosink
    if (useGlSink) {
        if (!gst_element_link_many(videoAppsrc, h264parse, videoDecode, videoConvert, glUpload, videoSink, nullptr)) {
            g_warning("MediaPipeline::initAppsrcPipeline — failed to link video branch (GL)");
            gst_object_unref(pipeline);
            return false;
        }
    } else {
        if (!gst_element_link_many(videoAppsrc, h264parse, videoDecode, videoConvert, videoSink, nullptr)) {
            g_warning("MediaPipeline::initAppsrcPipeline — failed to link video branch (fake)");
            gst_object_unref(pipeline);
            return false;
        }
    }

    // Audio branch: appsrc ! decodebin (dynamic pads) -> audioconvert ! audioresample ! autoaudiosink
    // Link up to decodebin; the rest is connected via pad-added signal
    if (!gst_element_link(audioAppsrc, audioDecode)) {
        g_warning("MediaPipeline::initAppsrcPipeline — failed to link audio appsrc to decodebin");
        gst_object_unref(pipeline);
        return false;
    }

    // Connect decodebin pad-added signal to complete the audio chain dynamically
    struct AudioPadHelper {
        static void callback(GstElement* /*decodebin*/, GstPad* pad, gpointer data) {
            auto* payload = static_cast<std::tuple<GstElement*, GstElement*, GstElement*>*>(data);
            GstElement* convert  = std::get<0>(*payload);
            GstElement* resample = std::get<1>(*payload);
            GstElement* sink     = std::get<2>(*payload);

            // Only connect audio pads
            GstCaps* padCaps = gst_pad_get_current_caps(pad);
            if (!padCaps) padCaps = gst_pad_query_caps(pad, nullptr);
            if (padCaps) {
                const GstStructure* s = gst_caps_get_structure(padCaps, 0);
                const gchar* mediaType = gst_structure_get_name(s);
                if (!g_str_has_prefix(mediaType, "audio/")) {
                    gst_caps_unref(padCaps);
                    return;
                }
                gst_caps_unref(padCaps);
            }

            GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
            if (!sinkPad || GST_PAD_IS_LINKED(sinkPad)) {
                if (sinkPad) gst_object_unref(sinkPad);
                return;
            }
            if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                g_warning("MediaPipeline::initAppsrcPipeline — audio pad-added link failed");
            }
            gst_object_unref(sinkPad);
            gst_element_link_many(convert, resample, sink, nullptr);
        }
    };

    auto* audioPadData = new std::tuple<GstElement*, GstElement*, GstElement*>(
        audioConvert, audioResample, audioSink);
    g_signal_connect(audioDecode, "pad-added",
                     G_CALLBACK(AudioPadHelper::callback), audioPadData);

    m_pipeline    = pipeline;
    m_audioSink   = audioSink;
    m_videoAppsrc = videoAppsrc;
    m_audioAppsrc = audioAppsrc;

    // Start in PAUSED state — AirPlayHandler transitions to PLAYING when first frame arrives
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_warning("MediaPipeline::initAppsrcPipeline — GST_STATE_CHANGE_FAILURE on PAUSED");
        gst_object_unref(m_pipeline);
        m_pipeline    = nullptr;
        m_audioSink   = nullptr;
        m_videoAppsrc = nullptr;
        m_audioAppsrc = nullptr;
        return false;
    }

    return true;
}

// ── Phase 5: URI-based pipeline (uridecodebin) ────────────────────────────────

bool MediaPipeline::initUriPipeline(void* qmlVideoItem) {
    // D-04, D-05: URI-based pipeline for DLNA media playback.
    // Pipeline: uridecodebin ! [video: videoconvert ! glupload ! qml6glsink (or fakesink)]
    //                          [audio: audioconvert ! audioresample ! volume ! autoaudiosink]
    // uridecodebin emits pad-added for each decoded stream — pads are connected
    // dynamically via the same pattern as initAppsrcPipeline.

    GstElement* pipeline      = gst_pipeline_new("uri-pipeline");
    GstElement* uridecodebin  = gst_element_factory_make("uridecodebin",   "urisrc");
    GstElement* audioConvert  = gst_element_factory_make("audioconvert",   "uri-audioconvert");
    GstElement* audioResample = gst_element_factory_make("audioresample",  "uri-audioresample");
    GstElement* volume        = gst_element_factory_make("volume",         "uri-volume");
    GstElement* audioSink     = gst_element_factory_make("autoaudiosink",  "uri-audiosink");

    const bool useGlSink  = (qmlVideoItem != nullptr);
    GstElement* videoConvert = gst_element_factory_make("videoconvert",  "uri-videoconvert");
    GstElement* glUpload     = useGlSink ? gst_element_factory_make("glupload",   "uri-glupload") : nullptr;
    GstElement* videoSink    = useGlSink
        ? gst_element_factory_make("qml6glsink", "uri-videosink")
        : gst_element_factory_make("fakesink",   "uri-videosink");

    if (!pipeline || !uridecodebin || !audioConvert || !audioResample || !volume || !audioSink ||
        !videoConvert || !videoSink || (useGlSink && !glUpload)) {
        g_warning("MediaPipeline::initUriPipeline — failed to create one or more GStreamer elements");
        if (pipeline)      gst_object_unref(pipeline);
        if (uridecodebin)  gst_object_unref(uridecodebin);
        if (audioConvert)  gst_object_unref(audioConvert);
        if (audioResample) gst_object_unref(audioResample);
        if (volume)        gst_object_unref(volume);
        if (audioSink)     gst_object_unref(audioSink);
        if (videoConvert)  gst_object_unref(videoConvert);
        if (glUpload)      gst_object_unref(glUpload);
        if (videoSink)     gst_object_unref(videoSink);
        return false;
    }

    // Configure qml6glsink widget before state change
    if (useGlSink) {
        g_object_set(videoSink, "widget", qmlVideoItem, nullptr);
    }

    // Add all elements to pipeline — uridecodebin is not linked to sinks yet;
    // dynamic pads will be linked via pad-added callback
    if (useGlSink) {
        gst_bin_add_many(GST_BIN(pipeline),
            uridecodebin, videoConvert, glUpload, videoSink,
            audioConvert, audioResample, volume, audioSink,
            nullptr);
    } else {
        gst_bin_add_many(GST_BIN(pipeline),
            uridecodebin, videoConvert, videoSink,
            audioConvert, audioResample, volume, audioSink,
            nullptr);
    }

    // Pre-link the static parts of audio chain: audioconvert ! audioresample ! volume ! autoaudiosink
    // These are linked now; uridecodebin audio pad will link to audioconvert's sink in pad-added
    if (!gst_element_link_many(audioConvert, audioResample, volume, audioSink, nullptr)) {
        g_warning("MediaPipeline::initUriPipeline — failed to pre-link audio chain");
        gst_object_unref(pipeline);
        return false;
    }

    // Pre-link video chain: videoconvert [! glupload] ! videosink
    if (useGlSink) {
        if (!gst_element_link_many(videoConvert, glUpload, videoSink, nullptr)) {
            g_warning("MediaPipeline::initUriPipeline — failed to pre-link video chain (GL)");
            gst_object_unref(pipeline);
            return false;
        }
    } else {
        if (!gst_element_link(videoConvert, videoSink)) {
            g_warning("MediaPipeline::initUriPipeline — failed to pre-link video chain (fake)");
            gst_object_unref(pipeline);
            return false;
        }
    }

    // Connect pad-added signal on uridecodebin to link audio/video pads dynamically.
    // Payload carries the first elements of each branch's pre-linked chain.
    struct UriPadHelper {
        GstElement* videoConvert;
        GstElement* audioConvert;
    };
    auto* padData = new UriPadHelper{videoConvert, audioConvert};

    struct UriPadAddedCallback {
        static void callback(GstElement* /*decodebin*/, GstPad* pad, gpointer data) {
            auto* payload = static_cast<UriPadHelper*>(data);

            // Determine pad type from caps
            GstCaps* padCaps = gst_pad_get_current_caps(pad);
            if (!padCaps) padCaps = gst_pad_query_caps(pad, nullptr);
            if (!padCaps) return;

            const GstStructure* s = gst_caps_get_structure(padCaps, 0);
            const gchar* mediaType = gst_structure_get_name(s);
            gst_caps_unref(padCaps);

            if (g_str_has_prefix(mediaType, "video/")) {
                GstPad* sinkPad = gst_element_get_static_pad(payload->videoConvert, "sink");
                if (sinkPad && !GST_PAD_IS_LINKED(sinkPad)) {
                    if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                        g_warning("MediaPipeline::initUriPipeline — video pad-added link failed");
                    }
                }
                if (sinkPad) gst_object_unref(sinkPad);
            } else if (g_str_has_prefix(mediaType, "audio/")) {
                GstPad* sinkPad = gst_element_get_static_pad(payload->audioConvert, "sink");
                if (sinkPad && !GST_PAD_IS_LINKED(sinkPad)) {
                    if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                        g_warning("MediaPipeline::initUriPipeline — audio pad-added link failed");
                    }
                }
                if (sinkPad) gst_object_unref(sinkPad);
            }
        }
    };

    g_signal_connect(uridecodebin, "pad-added",
                     G_CALLBACK(UriPadAddedCallback::callback), padData);

    m_uriPipeline  = pipeline;
    m_uriDecodebin = uridecodebin;
    m_uriAudioSink = audioSink;
    m_uriVolume    = volume;

    return true;
}

void MediaPipeline::setUri(const std::string& uri) {
    if (!m_uriPipeline || !m_uriDecodebin) {
        g_warning("MediaPipeline::setUri — URI pipeline not initialized");
        return;
    }
    // Stop to NULL before changing URI (Pitfall 4 from RESEARCH.md)
    gst_element_set_state(m_uriPipeline, GST_STATE_NULL);
    g_object_set(m_uriDecodebin, "uri", uri.c_str(), nullptr);
    // PAUSED prerolls the pipeline (buffers, but does not play)
    gst_element_set_state(m_uriPipeline, GST_STATE_PAUSED);
}

void MediaPipeline::playUri() {
    if (m_uriPipeline) {
        gst_element_set_state(m_uriPipeline, GST_STATE_PLAYING);
    }
}

void MediaPipeline::pauseUri() {
    if (m_uriPipeline) {
        gst_element_set_state(m_uriPipeline, GST_STATE_PAUSED);
    }
}

void MediaPipeline::stopUri() {
    if (m_uriPipeline) {
        gst_element_set_state(m_uriPipeline, GST_STATE_NULL);
    }
}

gint64 MediaPipeline::queryPosition() const {
    if (!m_uriPipeline) return -1;
    gint64 pos = -1;
    gst_element_query_position(m_uriPipeline, GST_FORMAT_TIME, &pos);
    return pos;
}

gint64 MediaPipeline::queryDuration() const {
    if (!m_uriPipeline) return -1;
    gint64 dur = -1;
    gst_element_query_duration(m_uriPipeline, GST_FORMAT_TIME, &dur);
    return dur;
}

void MediaPipeline::seekUri(gint64 positionNs) {
    if (!m_uriPipeline) return;
    gst_element_seek_simple(m_uriPipeline, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        positionNs);
}

void MediaPipeline::setVolume(double volume) {
    if (m_uriVolume) {
        g_object_set(m_uriVolume, "volume", volume, nullptr);
    }
    // Also set on appsrc pipeline audio sink if active (for consistency)
    if (m_audioSink) {
        g_object_set(m_audioSink, "volume", volume, nullptr);
    }
}

double MediaPipeline::getVolume() const {
    if (m_uriVolume) {
        gdouble vol = 1.0;
        g_object_get(m_uriVolume, "volume", &vol, nullptr);
        return static_cast<double>(vol);
    }
    return 1.0;
}

void MediaPipeline::setAudioCaps(const char* capsString) {
    if (!m_audioAppsrc || !capsString) return;
    GstCaps* caps = gst_caps_from_string(capsString);
    if (!caps) {
        g_warning("MediaPipeline::setAudioCaps — invalid caps string: %s", capsString);
        return;
    }
    g_object_set(m_audioAppsrc, "caps", caps, nullptr);
    gst_caps_unref(caps);
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
