#include "pipeline/MediaPipeline.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>
#include <glib.h>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace airshow {

// ── MediaPipeline ────────────────────────────────────────────────────────────

MediaPipeline::MediaPipeline() = default;

MediaPipeline::~MediaPipeline() {
    stop();
}

bool MediaPipeline::init(void* /*qmlVideoItem*/) {
    // D-05: Phase 1 uses videotestsrc + audiotestsrc to validate the pipeline.
    // D-06: Single shared pipeline — all future protocols converge on one pipeline.
    // Pipeline string concept:
    //   videotestsrc ! videoconvert ! appsink (→ VideoFrameCallback)
    //   audiotestsrc ! audioconvert ! autoaudiosink

    GstElement* pipeline     = gst_pipeline_new("airshow-pipeline");
    GstElement* videoSrc     = gst_element_factory_make("videotestsrc",  "videosrc");
    GstElement* videoConvert = gst_element_factory_make("videoconvert",  "videoconvert");
    GstElement* videoSink    = createVideoSink("videosink", /*realtime=*/false);
    GstElement* audioSrc     = gst_element_factory_make("audiotestsrc",  "audiosrc");
    GstElement* audioConvert = gst_element_factory_make("audioconvert",  "audioconvert");
    GstElement* audioSink    = gst_element_factory_make("autoaudiosink", "audiosink");

    if (!pipeline || !videoSrc || !videoConvert || !videoSink ||
        !audioSrc || !audioConvert || !audioSink) {
        g_warning("MediaPipeline::init — failed to create one or more GStreamer elements");
        if (pipeline)     gst_object_unref(pipeline);
        if (videoSrc)     gst_object_unref(videoSrc);
        if (videoConvert) gst_object_unref(videoConvert);
        if (videoSink)    gst_object_unref(videoSink);
        if (audioSrc)     gst_object_unref(audioSrc);
        if (audioConvert) gst_object_unref(audioConvert);
        if (audioSink)    gst_object_unref(audioSink);
        return false;
    }

    gst_bin_add_many(GST_BIN(pipeline),
        videoSrc, videoConvert, videoSink,
        audioSrc, audioConvert, audioSink,
        nullptr);

    if (!gst_element_link_many(videoSrc, videoConvert, videoSink, nullptr)) {
        g_warning("MediaPipeline::init — failed to link video branch");
        gst_object_unref(pipeline);
        return false;
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
        g_warning("MediaPipeline::init — GST_STATE_CHANGE_FAILURE (PLAYING)");
        gst_object_unref(m_pipeline);
        m_pipeline  = nullptr;
        m_audioSink = nullptr;
        return false;
    }

    m_needsPlay = false;
    return true;
}

void MediaPipeline::play() {
    // Main appsrc/test pipeline
    if (m_pipeline) {
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_warning("MediaPipeline::play — GST_STATE_CHANGE_FAILURE on m_pipeline");
        }
        m_needsPlay = false;
    }
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
        {
            std::lock_guard<std::mutex> lock(m_decoderMutex);
            m_activeDecoder = fallback;
        }
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
    std::lock_guard<std::mutex> lock(m_decoderMutex);
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
    stopCast();
    // Phase 8: clean up Miracast pipeline
    if (m_miracastPipeline) {
        gst_element_set_state(m_miracastPipeline, GST_STATE_NULL);
        gst_object_unref(m_miracastPipeline);
        m_miracastPipeline = nullptr;
    }
}

void MediaPipeline::setVideoFrameCallback(VideoFrameCallback cb)
{
    m_videoFrameCallback = std::move(cb);
}

// Static appsink new-sample callback — runs on GStreamer's streaming thread.
// Pulls the sample, maps the video frame, converts to QImage (RGBA), and
// invokes the registered VideoFrameCallback so the Qt item can render it.
GstFlowReturn MediaPipeline::onNewVideoSample(GstAppSink* sink, gpointer userData)
{
    auto* self = static_cast<MediaPipeline*>(userData);
    if (!self->m_videoFrameCallback) return GST_FLOW_OK;

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buf  = gst_sample_get_buffer(sample);
    GstCaps*   caps = gst_sample_get_caps(sample);

    GstVideoInfo info;
    if (caps && gst_video_info_from_caps(&info, caps)) {
        GstVideoFrame vframe;
        if (gst_video_frame_map(&vframe, &info, buf, GST_MAP_READ)) {
            const int    w      = GST_VIDEO_FRAME_WIDTH(&vframe);
            const int    h      = GST_VIDEO_FRAME_HEIGHT(&vframe);
            const int    stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
            const uchar* data   = static_cast<const uchar*>(
                                    GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0));

            // RGBA in GStreamer memory maps directly to QImage::Format_RGBA8888.
            QImage frame(data, w, h, stride, QImage::Format_RGBA8888);
            QImage copy = frame.copy();  // Deep copy before unmapping GstBuffer.

            gst_video_frame_unmap(&vframe);
            self->m_videoFrameCallback(std::move(copy));
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstElement* MediaPipeline::createVideoSink(const char* name, bool realtime)
{
    if (!m_videoFrameCallback)
        return gst_element_factory_make("fakesink", name);

    GstElement* sink = gst_element_factory_make("appsink", name);
    if (!sink) return nullptr;

    // Drop old frames when Qt is slow; RGBA maps directly onto QImage::Format_RGBA8888.
    GstCaps* caps = gst_caps_from_string("video/x-raw,format=RGBA");
    g_object_set(sink,
        "max-buffers", 2,
        "drop",        TRUE,
        "caps",        caps,
        nullptr);
    gst_caps_unref(caps);

    // async=FALSE is CRITICAL for live pipelines: without it, appsink returns
    // GST_STATE_CHANGE_ASYNC from PAUSED, blocking the pipeline in PAUSED forever
    // because a live branch can't complete preroll (e.g. audio decodebin with no caps yet).
    if (realtime) {
        g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);
    }

    // gst_app_sink_set_callbacks (not g_signal_connect) so the GstAppSink* signature
    // is enforced and the return value is handled.
    static GstAppSinkCallbacks appsinkCbs = {
        nullptr,                               // eos
        nullptr,                               // new_preroll
        MediaPipeline::onNewVideoSample,       // new_sample
        nullptr,                               // new_event (padding)
    };
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &appsinkCbs, this, nullptr);
    return sink;
}

bool MediaPipeline::initAppsrcPipeline(void* qmlVideoItem) {
    // Phase 4 appsrc-based pipeline for receiving live AirPlay A/V frames (D-03).
    //
    // Video branch:
    //   appsrc ! queue ! h264parse ! [vaapih264dec|avdec_h264] ! queue ! videoconvert
    //          ! video/x-raw,format=RGBA ! appsink
    //
    // Audio branch:
    //   appsrc ! queue ! decodebin ! audioconvert ! audioresample ! autoaudiosink
    //
    // Using appsink (not qml6glsink) avoids all GStreamer GL context sharing complexity
    // and the GL buffer pool deadlock that caused qml6glsink to stall after frame #1.
    // Decoded frames are delivered as RGBA QImages via m_videoFrameCallback, which
    // VideoFrameSink uses to render on Qt's scene graph thread — no shared GL state.
    //
    // queue elements are CRITICAL: without them, gst_app_src_push_buffer() blocks on the
    // UxPlay RAOP thread until the downstream sink releases the buffer, which requires the
    // Qt main thread. This cross-thread wait stalls after the first frame. Queues decouple
    // the producer (RAOP thread) from the consumer (decoder/renderer threads).

    GstElement* pipeline      = gst_pipeline_new("airshow-pipeline");
    GstElement* videoAppsrc   = gst_element_factory_make("appsrc",        "video_appsrc");
    GstElement* videoQueue    = gst_element_factory_make("queue",          "video_queue");
    GstElement* h264parse     = gst_element_factory_make("h264parse",      "h264parse");
    GstElement* decodeQueue   = gst_element_factory_make("queue",          "decode_queue");
    GstElement* videoConvert  = gst_element_factory_make("videoconvert",   "videoconvert");
    GstElement* audioAppsrc   = gst_element_factory_make("appsrc",         "audio_appsrc");
    GstElement* audioQueue    = gst_element_factory_make("queue",           "audio_queue");
    GstElement* audioDecode   = gst_element_factory_make("decodebin",      "audiodecode");
    GstElement* audioConvert  = gst_element_factory_make("audioconvert",   "audioconvert");
    GstElement* audioResample = gst_element_factory_make("audioresample",  "audioresample");
    GstElement* audioSink     = gst_element_factory_make("autoaudiosink",  "audiosink");

    // Configure queues for live screen mirroring — leaky=2 drops oldest when full so
    // we always display the latest frame rather than blocking the entire pipeline.
    // max-size-bytes=0 and max-size-time=0 disable byte/time limits; only buffer count limits.
    // videoQueue (encoded): hold a few encoded frames; drop oldest if decoder falls behind.
    // decodeQueue (decoded): decoded 1080p frames are ~3MB each — without leaky the queue
    //   fills after ~5 frames and permanently blocks avdec_h264. leaky=2 + max-buffers=2
    //   keeps only the 2 most recent decoded frames and drops older ones.
    if (videoQueue) {
        g_object_set(videoQueue,
            "max-size-buffers", 4,
            "max-size-bytes",   0,
            "max-size-time",    G_GUINT64_CONSTANT(0),
            "leaky",            2,   // drop oldest
            nullptr);
    }
    if (decodeQueue) {
        g_object_set(decodeQueue,
            "max-size-buffers", 2,
            "max-size-bytes",   0,
            "max-size-time",    G_GUINT64_CONSTANT(0),
            "leaky",            2,   // drop oldest
            nullptr);
    }

    // Video decoder: try hardware (vaapih264dec) first, fall back to avdec_h264 (D-12)
    GstElement* videoDecode = gst_element_factory_make("vaapih264dec", "videodecode");
    if (!videoDecode) {
        g_message("MediaPipeline::initAppsrcPipeline — vaapih264dec not available, falling back to avdec_h264");
        videoDecode = gst_element_factory_make("avdec_h264", "videodecode");
    }

    GstElement* videoSink = createVideoSink("videosink", /*realtime=*/true);

    // Verify all mandatory elements were created
    if (!pipeline || !videoAppsrc || !videoQueue || !h264parse || !decodeQueue ||
        !videoDecode || !videoConvert || !videoSink ||
        !audioAppsrc || !audioQueue || !audioDecode ||
        !audioConvert || !audioResample || !audioSink) {
        g_warning("MediaPipeline::initAppsrcPipeline — failed to create one or more GStreamer elements");
        if (pipeline)      gst_object_unref(pipeline);
        if (videoAppsrc)   gst_object_unref(videoAppsrc);
        if (videoQueue)    gst_object_unref(videoQueue);
        if (h264parse)     gst_object_unref(h264parse);
        if (decodeQueue)   gst_object_unref(decodeQueue);
        if (videoDecode)   gst_object_unref(videoDecode);
        if (videoConvert)  gst_object_unref(videoConvert);
        if (videoSink)     gst_object_unref(videoSink);
        if (audioAppsrc)   gst_object_unref(audioAppsrc);
        if (audioQueue)    gst_object_unref(audioQueue);
        if (audioDecode)   gst_object_unref(audioDecode);
        if (audioConvert)  gst_object_unref(audioConvert);
        if (audioResample) gst_object_unref(audioResample);
        if (audioSink)     gst_object_unref(audioSink);
        return false;
    }

    // Configure video appsrc for live screen mirroring:
    //   do-timestamp=TRUE — appsrc stamps each buffer with the pipeline running_time.
    //   is-live=TRUE      — never blocks; drops if downstream is slow.
    //   format=TIME       — PTS/DTS in nanoseconds.
    g_object_set(videoAppsrc,
        "stream-type",   0,
        "format",        GST_FORMAT_TIME,
        "is-live",       TRUE,
        "do-timestamp",  TRUE,
        nullptr);
    // UxPlay delivers one complete H.264 access unit per video_process callback.
    GstCaps* videoCaps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=au");
    g_object_set(videoAppsrc, "caps", videoCaps, nullptr);
    gst_caps_unref(videoCaps);

    // Configure audio appsrc: same live settings, caps set later via setAudioCaps()
    g_object_set(audioAppsrc,
        "stream-type",  0,
        "format",       GST_FORMAT_TIME,
        "is-live",      TRUE,
        "do-timestamp", TRUE,
        nullptr);

    // Add all elements to the pipeline
    gst_bin_add_many(GST_BIN(pipeline),
        videoAppsrc, videoQueue, h264parse, videoDecode, decodeQueue,
        videoConvert, videoSink,
        audioAppsrc, audioQueue, audioDecode, audioConvert, audioResample, audioSink,
        nullptr);

    // Video branch: appsrc ! videoQueue ! h264parse ! decoder ! decodeQueue ! videoconvert ! appsink
    if (!gst_element_link_many(videoAppsrc, videoQueue, h264parse, videoDecode,
                               decodeQueue, videoConvert, videoSink, nullptr)) {
        g_warning("MediaPipeline::initAppsrcPipeline — failed to link video branch");
        gst_object_unref(pipeline);
        return false;
    }

    // Audio branch: appsrc ! queue ! decodebin (dynamic pads) -> audioconvert ! audioresample ! autoaudiosink
    // Link up to decodebin; the rest is connected via pad-added signal
    if (!gst_element_link_many(audioAppsrc, audioQueue, audioDecode, nullptr)) {
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


    // async=FALSE on audio sink child: autoaudiosink wraps pulsesink/alsasink which
    // returns ASYNC for PAUSED (opening audio device). Without this the pipeline
    // gets stuck in PAUSED forever when audio decodebin has no caps yet.
    // autoaudiosink itself has no "async" property — must set it on the child sink
    // via "element-added" signal, which fires when the real sink is created.
    g_signal_connect(audioSink, "element-added",
        G_CALLBACK(+[](GstBin*, GstElement* child, gpointer) {
            g_object_set(child, "async", FALSE, "sync", FALSE, nullptr);
        }), nullptr);

    // decodebin async-handling=FALSE: prevents decodebin from propagating ASYNC
    // from its children, so the pipeline doesn't wait for format detection
    // before completing state transitions.
    g_object_set(audioDecode, "async-handling", FALSE, nullptr);

    m_pipeline    = pipeline;
    m_audioSink   = audioSink;
    m_videoAppsrc = videoAppsrc;
    m_audioAppsrc = audioAppsrc;

    // Start in PLAYING immediately — do-timestamp=TRUE on appsrc means the pipeline
    // clock is the timing source, so we need it running before the first buffer arrives.
    // (Previously held in PAUSED waiting for first frame; that approach broke because
    //  the clock wasn't running when we tried to timestamp frames.)
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_warning("MediaPipeline::initAppsrcPipeline — GST_STATE_CHANGE_FAILURE on PLAYING");
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

bool MediaPipeline::initUriPipeline(void* /*qmlVideoItem*/) {
    // D-04, D-05: URI-based pipeline for DLNA media playback.
    // Pipeline: uridecodebin ! [video: videoconvert ! appsink (or fakesink)]
    //                          [audio: audioconvert ! audioresample ! volume ! autoaudiosink]
    // uridecodebin emits pad-added for each decoded stream — pads are connected
    // dynamically via the same pattern as initAppsrcPipeline.

    GstElement* pipeline      = gst_pipeline_new("uri-pipeline");
    GstElement* uridecodebin  = gst_element_factory_make("uridecodebin",   "urisrc");
    GstElement* audioConvert  = gst_element_factory_make("audioconvert",   "uri-audioconvert");
    GstElement* audioResample = gst_element_factory_make("audioresample",  "uri-audioresample");
    GstElement* volume        = gst_element_factory_make("volume",         "uri-volume");
    GstElement* audioSink     = gst_element_factory_make("autoaudiosink",  "uri-audiosink");

    GstElement* videoConvert = gst_element_factory_make("videoconvert",  "uri-videoconvert");
    GstElement* videoSink    = createVideoSink("uri-videosink", /*realtime=*/false);

    if (!pipeline || !uridecodebin || !audioConvert || !audioResample || !volume || !audioSink ||
        !videoConvert || !videoSink) {
        g_warning("MediaPipeline::initUriPipeline — failed to create one or more GStreamer elements");
        if (pipeline)      gst_object_unref(pipeline);
        if (uridecodebin)  gst_object_unref(uridecodebin);
        if (audioConvert)  gst_object_unref(audioConvert);
        if (audioResample) gst_object_unref(audioResample);
        if (volume)        gst_object_unref(volume);
        if (audioSink)     gst_object_unref(audioSink);
        if (videoConvert)  gst_object_unref(videoConvert);
        if (videoSink)     gst_object_unref(videoSink);
        return false;
    }

    // Add all elements to pipeline — uridecodebin is not linked to sinks yet;
    // dynamic pads will be linked via pad-added callback
    gst_bin_add_many(GST_BIN(pipeline),
        uridecodebin, videoConvert, videoSink,
        audioConvert, audioResample, volume, audioSink,
        nullptr);

    // Pre-link the static parts of audio chain: audioconvert ! audioresample ! volume ! autoaudiosink
    // These are linked now; uridecodebin audio pad will link to audioconvert's sink in pad-added
    if (!gst_element_link_many(audioConvert, audioResample, volume, audioSink, nullptr)) {
        g_warning("MediaPipeline::initUriPipeline — failed to pre-link audio chain");
        gst_object_unref(pipeline);
        return false;
    }

    // Pre-link video chain: videoconvert ! videosink
    if (!gst_element_link(videoConvert, videoSink)) {
        g_warning("MediaPipeline::initUriPipeline — failed to pre-link video chain");
        gst_object_unref(pipeline);
        return false;
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
        g_str_has_prefix(name, "vah")      ||  // Linux va plugin (vah264dec)
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

    {
        std::lock_guard<std::mutex> lock(self->m_decoderMutex);
        self->m_activeDecoder = info;
    }
    if (self->m_decoderCallback) {
        self->m_decoderCallback(info);
    }
}

void MediaPipeline::setQmlVideoItem(void* item) {
    m_qmlVideoItem = item;
    g_message("MediaPipeline: QML video item stored for deferred pipeline creation");
}

// ── Google Cast Streaming playback ────────────────────────────────────────────
//
// CastStreamingReceiver handles the network side (RTP, AES-CTR, frame assembly,
// RTCP feedback) and hands over complete encoded frames. This pipeline only decodes
// and renders them:
//   appsrc(video caps) ! queue ! decodebin ~> queue ! videoconvert ! appsink
//   appsrc(audio caps) ! queue ! decodebin ~> audioconvert ! audioresample ! autoaudiosink

namespace {

// decodebin pad-added: link pads of the given media type ("video/" or "audio/")
// to the first element of the pre-linked downstream chain.
struct DecodebinLink {
    const char* mediaPrefix;
    GstElement* target;

    static void onPadAdded(GstElement* /*decodebin*/, GstPad* pad, gpointer data) {
        auto* self = static_cast<DecodebinLink*>(data);
        GstCaps* caps = gst_pad_get_current_caps(pad);
        if (!caps) caps = gst_pad_query_caps(pad, nullptr);
        if (!caps) return;
        const bool matches = g_str_has_prefix(
            gst_structure_get_name(gst_caps_get_structure(caps, 0)), self->mediaPrefix);
        gst_caps_unref(caps);
        if (!matches) return;

        GstPad* sinkPad = gst_element_get_static_pad(self->target, "sink");
        if (sinkPad && !GST_PAD_IS_LINKED(sinkPad) &&
            gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
            g_warning("MediaPipeline: Cast %s decodebin pad link failed", self->mediaPrefix);
        }
        if (sinkPad) gst_object_unref(sinkPad);
    }

    static void free(gpointer data, GClosure*) { delete static_cast<DecodebinLink*>(data); }
};

GstElement* makeLiveAppsrc(const char* name, const std::string& caps) {
    GstElement* src = gst_element_factory_make("appsrc", name);
    if (!src) return nullptr;
    GstCaps* c = gst_caps_from_string(caps.c_str());
    g_object_set(src,
        "stream-type",  0,
        "format",       GST_FORMAT_TIME,
        "is-live",      TRUE,
        "do-timestamp", TRUE,
        "caps",         c,
        nullptr);
    gst_caps_unref(c);
    return src;
}

GstElement* makeLeakyQueue(const char* name, guint maxBuffers) {
    GstElement* q = gst_element_factory_make("queue", name);
    if (q) {
        g_object_set(q,
            "max-size-buffers", maxBuffers,
            "max-size-bytes",   0,
            "max-size-time",    G_GUINT64_CONSTANT(0),
            "leaky",            2,   // drop oldest — always show the latest frame
            nullptr);
    }
    return q;
}

} // namespace

bool MediaPipeline::initCastPipeline(const std::string& videoCaps, const std::string& audioCaps) {
    stopCast();
    if (videoCaps.empty() && audioCaps.empty()) return false;

    GstElement* pipeline = gst_pipeline_new("cast-pipeline");
    if (!pipeline) return false;

    auto fail = [pipeline](const char* what) {
        g_warning("MediaPipeline::initCastPipeline — %s", what);
        gst_object_unref(pipeline);
        return false;
    };

    GstElement* videoSrc = nullptr;
    if (!videoCaps.empty()) {
        videoSrc                 = makeLiveAppsrc("cast-video-src", videoCaps);
        GstElement* inQueue      = makeLeakyQueue("cast-video-queue", 8);
        GstElement* decode       = gst_element_factory_make("decodebin", "cast-video-decode");
        GstElement* outQueue     = makeLeakyQueue("cast-video-outqueue", 2);
        GstElement* convert      = gst_element_factory_make("videoconvert", "cast-videoconvert");
        GstElement* sink         = createVideoSink("cast-videosink", /*realtime=*/true);
        if (!videoSrc || !inQueue || !decode || !outQueue || !convert || !sink) {
            for (GstElement* e : {videoSrc, inQueue, decode, outQueue, convert, sink})
                if (e) gst_object_unref(e);
            return fail("failed to create video elements");
        }
        gst_bin_add_many(GST_BIN(pipeline), videoSrc, inQueue, decode, outQueue, convert, sink, nullptr);
        if (!gst_element_link_many(videoSrc, inQueue, decode, nullptr) ||
            !gst_element_link_many(outQueue, convert, sink, nullptr)) {
            return fail("failed to link video branch");
        }
        g_object_set(decode, "async-handling", FALSE, nullptr);
        g_signal_connect_data(decode, "pad-added", G_CALLBACK(DecodebinLink::onPadAdded),
                              new DecodebinLink{"video/", outQueue}, DecodebinLink::free,
                              GConnectFlags(0));
    }

    GstElement* audioSrc = nullptr;
    if (!audioCaps.empty()) {
        audioSrc             = makeLiveAppsrc("cast-audio-src", audioCaps);
        GstElement* queue    = makeLeakyQueue("cast-audio-queue", 16);
        GstElement* decode   = gst_element_factory_make("decodebin", "cast-audio-decode");
        GstElement* convert  = gst_element_factory_make("audioconvert", "cast-audioconvert");
        GstElement* resample = gst_element_factory_make("audioresample", "cast-audioresample");
        GstElement* sink     = gst_element_factory_make("autoaudiosink", "cast-audiosink");
        if (!audioSrc || !queue || !decode || !convert || !resample || !sink) {
            for (GstElement* e : {audioSrc, queue, decode, convert, resample, sink})
                if (e) gst_object_unref(e);
            return fail("failed to create audio elements");
        }
        gst_bin_add_many(GST_BIN(pipeline), audioSrc, queue, decode, convert, resample, sink, nullptr);
        if (!gst_element_link_many(audioSrc, queue, decode, nullptr) ||
            !gst_element_link_many(convert, resample, sink, nullptr)) {
            return fail("failed to link audio branch");
        }
        g_object_set(decode, "async-handling", FALSE, nullptr);
        g_signal_connect_data(decode, "pad-added", G_CALLBACK(DecodebinLink::onPadAdded),
                              new DecodebinLink{"audio/", convert}, DecodebinLink::free,
                              GConnectFlags(0));
        // Same as the AirPlay pipeline: the real audio sink inside autoaudiosink must not
        // wait for preroll, or the live pipeline stalls in PAUSED.
        g_signal_connect(sink, "element-added",
            G_CALLBACK(+[](GstBin*, GstElement* child, gpointer) {
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(child), "async"))
                    g_object_set(child, "async", FALSE, "sync", FALSE, nullptr);
            }), nullptr);
        m_castAudioSink = sink;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        m_castAudioSink = nullptr;
        return fail("GST_STATE_CHANGE_FAILURE on PLAYING");
    }

    m_castPipeline = pipeline;
    m_castVideoSrc = videoSrc;
    m_castAudioSrc = audioSrc;
    g_message("MediaPipeline: Cast pipeline started (video='%s' audio='%s')",
              videoCaps.c_str(), audioCaps.c_str());
    return true;
}

static void pushFrame(GstElement* appsrc, const uint8_t* data, size_t size, bool keyFrame) {
    if (!appsrc || !data || size == 0) return;
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    gst_buffer_fill(buf, 0, data, size);
    if (!keyFrame) GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
    gst_app_src_push_buffer(GST_APP_SRC(appsrc), buf);  // takes ownership
}

void MediaPipeline::pushCastVideoFrame(const uint8_t* data, size_t size, bool keyFrame) {
    pushFrame(m_castVideoSrc, data, size, keyFrame);
}

void MediaPipeline::pushCastAudioFrame(const uint8_t* data, size_t size) {
    pushFrame(m_castAudioSrc, data, size, /*keyFrame=*/true);
}

void MediaPipeline::stopCast() {
    if (!m_castPipeline) return;
    gst_element_set_state(m_castPipeline, GST_STATE_NULL);
    gst_object_unref(m_castPipeline);
    m_castPipeline  = nullptr;
    m_castVideoSrc  = nullptr;
    m_castAudioSrc  = nullptr;
    m_castAudioSink = nullptr;
}

// ── Phase 8: Miracast MPEG-TS/RTP receive pipeline ────────────────────────────

bool MediaPipeline::initMiracastPipeline(void* /*qmlVideoItem*/, int udpPort) {
    // D-09, D-10: Miracast MPEG-TS/RTP receive pipeline (RESEARCH.md Pattern 4).
    //
    // Static chain: udpsrc ! rtpmp2tdepay ! tsparse ! tsdemux (dynamic pads)
    // Video branch (pad-added): queue ! h264parse ! [vaapidecodebin|avdec_h264]
    //                           ! videoconvert ! appsink  (or fakesink)
    // Audio branch (pad-added): queue ! decodebin (AAC or LPCM — we advertise both)
    //                           ! audioconvert ! audioresample ! autoaudiosink
    //
    // tsdemux emits dynamic pads when it identifies TS stream contents via PAT/PMT.
    // Pre-create video/audio branches and add to bin before pad-added fires (same
    // pattern as initUriPipeline Phase 5 — D-05).

    // Clean up any previous Miracast pipeline
    if (m_miracastPipeline) {
        stopMiracast();
    }

    GstElement* pipeline    = gst_pipeline_new("miracast-pipeline");

    // Static source chain: udpsrc ! rtpmp2tdepay ! tsparse ! tsdemux
    GstElement* udpsrc      = gst_element_factory_make("udpsrc",       "mir-udpsrc");
    GstElement* rtpmp2tdepay = gst_element_factory_make("rtpmp2tdepay", "mir-rtpmp2tdepay");
    GstElement* tsparse     = gst_element_factory_make("tsparse",      "mir-tsparse");
    GstElement* tsdemux     = gst_element_factory_make("tsdemux",      "mir-tsdemux");

    // Video branch (pre-created, linked in pad-added)
    GstElement* videoQueue  = gst_element_factory_make("queue",        "mir-videoqueue");
    GstElement* h264parse   = gst_element_factory_make("h264parse",    "mir-h264parse");

    // Hardware decoder: try vaapidecodebin first, fall back to avdec_h264 (same as AirPlay D-12)
    GstElement* videoDecode = gst_element_factory_make("vaapidecodebin", "mir-videodecode");
    if (!videoDecode) {
        g_message("MediaPipeline::initMiracastPipeline — vaapidecodebin not available, "
                  "falling back to avdec_h264");
        videoDecode = gst_element_factory_make("avdec_h264", "mir-videodecode");
    }

    GstElement* videoConvert = gst_element_factory_make("videoconvert", "mir-videoconvert");

    // Keeps clock sync (realtime=false) like the qml6glsink it replaces, so video is
    // presented at the stream's PTS alongside the audio branch.
    GstElement* videoSink  = createVideoSink("mir-videosink", /*realtime=*/false);

    // Audio branch (pre-created, linked in pad-added)
    GstElement* audioQueue    = gst_element_factory_make("queue",        "mir-audioqueue");
    GstElement* audioDecode   = gst_element_factory_make("decodebin",    "mir-audiodecode");
    GstElement* audioConvert  = gst_element_factory_make("audioconvert", "mir-audioconvert");
    GstElement* audioResample = gst_element_factory_make("audioresample","mir-audioresample");
    GstElement* audioSink     = gst_element_factory_make("autoaudiosink","mir-audiosink");

    // Verify all mandatory elements were created
    const bool allCreated = pipeline && udpsrc && rtpmp2tdepay && tsparse && tsdemux &&
                            videoQueue && h264parse && videoDecode && videoConvert && videoSink &&
                            audioQueue && audioDecode && audioConvert &&
                            audioResample && audioSink;

    if (!allCreated) {
        g_warning("MediaPipeline::initMiracastPipeline — failed to create one or more GStreamer elements");
        // Unref any successfully created elements to avoid leaks
        if (pipeline)      gst_object_unref(pipeline);
        if (udpsrc)        gst_object_unref(udpsrc);
        if (rtpmp2tdepay)  gst_object_unref(rtpmp2tdepay);
        if (tsparse)       gst_object_unref(tsparse);
        if (tsdemux)       gst_object_unref(tsdemux);
        if (videoQueue)    gst_object_unref(videoQueue);
        if (h264parse)     gst_object_unref(h264parse);
        if (videoDecode)   gst_object_unref(videoDecode);
        if (videoConvert)  gst_object_unref(videoConvert);
        if (videoSink)     gst_object_unref(videoSink);
        if (audioQueue)    gst_object_unref(audioQueue);
        if (audioDecode)   gst_object_unref(audioDecode);
        if (audioConvert)  gst_object_unref(audioConvert);
        if (audioResample) gst_object_unref(audioResample);
        if (audioSink)     gst_object_unref(audioSink);
        return false;
    }

    // Configure udpsrc: bind to the negotiated RTP port
    // caps describe the incoming RTP stream: MP2T (MPEG-TS) with 90kHz clock
    GstCaps* udpCaps = gst_caps_from_string(
        "application/x-rtp,media=video,clock-rate=90000,encoding-name=MP2T");
    g_object_set(udpsrc,
        "port", udpPort,
        "caps", udpCaps,
        nullptr);
    gst_caps_unref(udpCaps);

    // Add all elements to the pipeline bin before linking
    gst_bin_add_many(GST_BIN(pipeline),
        udpsrc, rtpmp2tdepay, tsparse, tsdemux,
        videoQueue, h264parse, videoDecode, videoConvert, videoSink,
        audioQueue, audioDecode, audioConvert, audioResample, audioSink,
        nullptr);

    // Link the static chain: udpsrc ! rtpmp2tdepay ! tsparse ! tsdemux
    // Do NOT link tsdemux output yet — tsdemux pads are dynamic (Pitfall 4)
    if (!gst_element_link_many(udpsrc, rtpmp2tdepay, tsparse, tsdemux, nullptr)) {
        g_warning("MediaPipeline::initMiracastPipeline — failed to link static source chain");
        gst_object_unref(pipeline);
        return false;
    }

    // Pre-link the video branch internal chain (up to videosink) — not yet connected to tsdemux
    if (!gst_element_link_many(videoQueue, h264parse, videoDecode, videoConvert, videoSink, nullptr)) {
        g_warning("MediaPipeline::initMiracastPipeline — failed to link video branch");
        gst_object_unref(pipeline);
        return false;
    }

    // Pre-link the audio branch internal chain
    // Sources may pick either codec from our M3 wfd_audio_codecs (LPCM is mandatory in
    // the Wi-Fi Display spec); decodebin selects avdec_aac or dvdlpcmdec accordingly.
    g_object_set(audioDecode, "async-handling", FALSE, nullptr);
    g_signal_connect_data(audioDecode, "pad-added", G_CALLBACK(DecodebinLink::onPadAdded),
                          new DecodebinLink{"audio/", audioConvert}, DecodebinLink::free,
                          GConnectFlags(0));
    if (!gst_element_link(audioQueue, audioDecode) ||
        !gst_element_link_many(audioConvert, audioResample, audioSink, nullptr)) {
        g_warning("MediaPipeline::initMiracastPipeline — failed to link audio branch");
        gst_object_unref(pipeline);
        return false;
    }

    // Connect tsdemux pad-added signal to dynamically link video/audio branches
    // Detect pad type from GstCaps: "video/x-h264" -> video queue, "audio/*" -> audio queue
    // (RESEARCH.md Pitfall 4, Pattern 4)
    struct MiracastPadHelper {
        GstElement* videoQueue;
        GstElement* audioQueue;

        static void callback(GstElement* /*tsdemux*/, GstPad* pad, gpointer data) {
            auto* self = static_cast<MiracastPadHelper*>(data);

            // Determine pad type from caps
            GstCaps* caps = gst_pad_get_current_caps(pad);
            if (!caps) caps = gst_pad_query_caps(pad, nullptr);
            if (!caps) return;

            const GstStructure* s = gst_caps_get_structure(caps, 0);
            const gchar* mediaType = gst_structure_get_name(s);
            gst_caps_unref(caps);

            if (g_str_has_prefix(mediaType, "video/x-h264")) {
                // Video pad: link to videoQueue sink
                GstPad* sinkPad = gst_element_get_static_pad(self->videoQueue, "sink");
                if (sinkPad && !GST_PAD_IS_LINKED(sinkPad)) {
                    if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                        g_warning("MiracastPadHelper: failed to link video pad to queue");
                    }
                }
                if (sinkPad) gst_object_unref(sinkPad);
            } else if (g_str_has_prefix(mediaType, "audio/")) {
                // Audio pad: link to audioQueue sink
                GstPad* sinkPad = gst_element_get_static_pad(self->audioQueue, "sink");
                if (sinkPad && !GST_PAD_IS_LINKED(sinkPad)) {
                    if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                        g_warning("MiracastPadHelper: failed to link audio pad to queue");
                    }
                }
                if (sinkPad) gst_object_unref(sinkPad);
            } else {
                g_message("MiracastPadHelper: ignoring unknown pad type '%s'", mediaType);
            }
        }
    };

    auto* padHelper = new MiracastPadHelper{videoQueue, audioQueue};
    g_signal_connect(tsdemux, "pad-added", G_CALLBACK(MiracastPadHelper::callback), padHelper);

    m_miracastPipeline = pipeline;

    // Start in PAUSED — MiracastHandler transitions to PLAYING when PLAY is received (M7)
    GstStateChangeReturn ret = gst_element_set_state(m_miracastPipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_warning("MediaPipeline::initMiracastPipeline — GST_STATE_CHANGE_FAILURE on PAUSED");
        gst_element_set_state(m_miracastPipeline, GST_STATE_NULL);
        gst_object_unref(m_miracastPipeline);
        m_miracastPipeline = nullptr;
        return false;
    }

    g_message("MediaPipeline::initMiracastPipeline — pipeline created and PAUSED (udpPort=%d)", udpPort);
    return true;
}

void MediaPipeline::stopMiracast() {
    if (m_miracastPipeline) {
        gst_element_set_state(m_miracastPipeline, GST_STATE_NULL);
        gst_object_unref(m_miracastPipeline);
        m_miracastPipeline = nullptr;
        g_message("MediaPipeline::stopMiracast — pipeline stopped and cleaned up");
    }
}

} // namespace airshow
