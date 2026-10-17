// Wave 0 test stubs for Phase 1 Foundation
// Tests are structured to match VALIDATION.md per-task verification map.
// Each test will be filled in by Plans 02 and 03 once MediaPipeline is implemented.
//
// Test filter names MUST match the ctest -R patterns from VALIDATION.md:
//   ctest -R test_video_pipeline
//   ctest -R test_audio_pipeline
//   ctest -R test_mute_toggle
//   ctest -R test_decoder_detection

#include <gtest/gtest.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "pipeline/MediaPipeline.h"
#include "pipeline/DecoderInfo.h"

// Headless CI machines (e.g. the Windows runner) have no audio output device, so
// autoaudiosink can't open one. Tests that need real audio output skip there.
static bool audioOutputAvailable() {
    GstElement* p = gst_parse_launch("audiotestsrc num-buffers=1 ! autoaudiosink", nullptr);
    if (!p) return false;
    bool ok = gst_element_set_state(p, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE &&
              gst_element_get_state(p, nullptr, nullptr, 2 * GST_SECOND) != GST_STATE_CHANGE_FAILURE;
    if (ok) {
        GstBus* bus = gst_element_get_bus(p);
        GstMessage* err = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
        if (err) { ok = false; gst_message_unref(err); }
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ok;
}

class PipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // GStreamer must be initialised before any pipeline test
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
    }
};

// FOUND-04: Mute toggle sets volume to 0; unmute restores to 1.0
TEST_F(PipelineTest, test_mute_toggle) {
    airshow::MediaPipeline pipeline;
    // Default state: not muted
    EXPECT_FALSE(pipeline.isMuted());
    // Mute
    pipeline.setMuted(true);
    EXPECT_TRUE(pipeline.isMuted());
    // Unmute
    pipeline.setMuted(false);
    EXPECT_FALSE(pipeline.isMuted());
}

// FOUND-02: GStreamer pipeline renders videotestsrc frames
TEST_F(PipelineTest, test_video_pipeline) {
    if (!audioOutputAvailable()) GTEST_SKIP() << "no audio output device";
    airshow::MediaPipeline pipeline;
    // No frame callback registered → fakesink video branch (headless test mode)
    bool ok = pipeline.init(nullptr);
    ASSERT_TRUE(ok) << "MediaPipeline::init() failed — check GStreamer plugin availability";

    // Use gstPipeline() getter to get a non-null pointer for state check
    GstElement* gstPipeline = pipeline.gstPipeline();
    ASSERT_NE(gstPipeline, nullptr) << "gstPipeline() must be non-null after successful init()";

    // Wait up to 2 seconds for PLAYING state
    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    GstStateChangeReturn ret = gst_element_get_state(
        gstPipeline, &state, &pending, 2 * GST_SECOND);
    EXPECT_NE(ret, GST_STATE_CHANGE_FAILURE) << "Pipeline state change failed";
    EXPECT_EQ(state, GST_STATE_PLAYING) << "Pipeline should be in PLAYING state";

    pipeline.stop();
    // After stop, a second stop must not crash (double-stop guard)
    pipeline.stop();
}

// FOUND-03: audiotestsrc plays through autoaudiosink
TEST_F(PipelineTest, test_audio_pipeline) {
    airshow::MediaPipeline pipeline;
    bool ok = pipeline.init(nullptr);
    ASSERT_TRUE(ok) << "MediaPipeline::init() failed — audio branch unavailable";
    // init() success implies autoaudiosink element was created and linked.
    // Volume should be 1.0 by default (not muted).
    EXPECT_FALSE(pipeline.isMuted());
    pipeline.stop();
}

// FOUND-05: decodebin selects a decoder and logs its name
TEST_F(PipelineTest, test_decoder_detection) {
    airshow::MediaPipeline pipeline;

    // The callback runs on GStreamer's streaming thread, possibly more than once
    // (decodebin can try a hardware decoder, then fall back).
    std::atomic<bool> callbackFired{false};
    pipeline.setDecoderSelectedCallback(
        [&callbackFired](const airshow::DecoderInfo&) { callbackFired = true; });

    bool ok = pipeline.initDecoderPipeline();
    ASSERT_TRUE(ok) << "initDecoderPipeline() failed — check x264enc and decodebin availability";

    // Wait up to 3 seconds for the decoder element-added callback to fire
    // (decodebin negotiates format asynchronously after GST_STATE_PLAYING)
    int waited = 0;
    while (!callbackFired && waited < 30) {
        g_usleep(100000);  // 100ms
        waited++;
    }

    // D-11 + D-12: Either hardware or software decoder must have been selected
    ASSERT_TRUE(pipeline.activeDecoder().has_value())
        << "activeDecoder() is empty — decodebin did not select a decoder within 3s";

    // Copy: activeDecoder() returns by value, so a reference would dangle.
    const std::string name = pipeline.activeDecoder()->elementName;
    EXPECT_FALSE(name.empty()) << "Decoder element name must not be empty";

    // Verify the name is a known decoder (D-11 list + software fallback D-12)
    static const std::set<std::string> knownDecoders = {
        "vaapih264dec",   // Linux VAAPI
        "vaaph264dec",    // alternative VAAPI name
        "vah264dec",      // GStreamer va plugin (replaces vaapi)
        "nvh264dec",      // NVIDIA NVDEC
        "nvdec",          // NVIDIA NVDEC (generic)
        "vtdec",          // macOS VideoToolbox
        "vtdec_hw",       // macOS VideoToolbox hardware
        "d3d11h264dec",   // Windows D3D11
        "mfh264dec",      // Windows Media Foundation
        "avdec_h264",     // Software fallback (D-12)
    };
    EXPECT_TRUE(knownDecoders.count(name) > 0)
        << "Unexpected decoder name: " << name
        << " — add it to knownDecoders if it is a valid hardware decoder";

    // Log which path was taken (informational)
    if (pipeline.activeDecoder()->type == airshow::DecoderType::Hardware) {
        std::cout << "[INFO] Hardware decoder selected: " << name << std::endl;
    } else {
        std::cout << "[INFO] Software decoder selected: " << name
                  << " (hardware unavailable on this machine — expected)" << std::endl;
    }

    pipeline.stop();
}

// Smoke test: verifies required GStreamer plugins are present
TEST(SmokeTest, required_plugins_available) {
    // gst_registry_check_feature_version requires an initialised GStreamer
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    struct { const char* name; const char* pkg; } plugins[] = {
        {"appsink",       "gstreamer1.0-plugins-base"},
        {"videotestsrc",  "gstreamer1.0-plugins-base"},
        {"audiotestsrc",  "gstreamer1.0-plugins-base"},
        {"autoaudiosink", "gstreamer1.0-plugins-good"},
        {"avdec_h264",    "gstreamer1.0-libav"},
    };
    for (auto& p : plugins) {
        EXPECT_TRUE(
            gst_registry_check_feature_version(
                gst_registry_get(), p.name, 1, 20, 0))
            << "Missing GStreamer plugin: " << p.name
            << " (install: " << p.pkg << ")";
    }
}

// Encode a few frames with a GStreamer launch line ending in "appsink name=out"
// and return each encoded buffer (bytes + whether it is a key frame).
struct EncodedFrame { std::vector<uint8_t> bytes; bool key; };
static std::vector<EncodedFrame> encodeFrames(const char* launch) {
    std::vector<EncodedFrame> frames;
    GstElement* p = gst_parse_launch(launch, nullptr);
    if (!p) return frames;
    GstElement* out = gst_bin_get_by_name(GST_BIN(p), "out");
    gst_element_set_state(p, GST_STATE_PLAYING);
    while (GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(out))) {
        GstBuffer* buf = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
            frames.push_back({std::vector<uint8_t>(map.data, map.data + map.size),
                              !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT)});
            gst_buffer_unmap(buf, &map);
        }
        gst_sample_unref(sample);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(out);
    gst_object_unref(p);
    return frames;
}

// Cast Streaming playback: complete encoded VP8 frames pushed into the Cast
// pipeline (as CastStreamingReceiver does) come out as decoded RGBA images.
TEST_F(PipelineTest, test_cast_vp8_frames_render) {
    auto frames = encodeFrames(
        "videotestsrc num-buffers=15 ! video/x-raw,width=320,height=240,framerate=30/1 "
        "! vp8enc deadline=1 keyframe-max-dist=5 ! appsink name=out sync=false");
    ASSERT_GE(frames.size(), 10u) << "vp8enc unavailable?";
    ASSERT_TRUE(frames.front().key);

    std::atomic<int> rendered{0};
    std::atomic<int> width{0};
    airshow::MediaPipeline pipeline;
    pipeline.setVideoFrameCallback([&](QImage img) {
        width = img.width();
        ++rendered;
    });
    ASSERT_TRUE(pipeline.initCastPipeline("video/x-vp8", ""));

    for (const auto& f : frames) {
        pipeline.pushCastVideoFrame(f.bytes.data(), f.bytes.size(), f.key);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (int i = 0; i < 300 && rendered < 5; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_GE(rendered.load(), 5);
    EXPECT_EQ(width.load(), 320);
    pipeline.stopCast();
    EXPECT_EQ(pipeline.castPipeline(), nullptr);
}

// Opus audio frames are accepted by the Cast audio branch without errors.
TEST_F(PipelineTest, test_cast_opus_audio_branch) {
    if (!audioOutputAvailable()) GTEST_SKIP() << "no audio output device";
    auto frames = encodeFrames(
        "audiotestsrc num-buffers=20 ! audio/x-raw,rate=48000,channels=2 "
        "! opusenc ! appsink name=out sync=false");
    ASSERT_GE(frames.size(), 5u) << "opusenc unavailable?";

    airshow::MediaPipeline pipeline;
    ASSERT_TRUE(pipeline.initCastPipeline(
        "", "audio/x-opus,channel-mapping-family=0,channels=2,rate=48000"));
    for (const auto& f : frames)
        pipeline.pushCastAudioFrame(f.bytes.data(), f.bytes.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    GstBus* bus = gst_element_get_bus(pipeline.castPipeline());
    GstMessage* err = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (err) {
        GError* e = nullptr;
        gst_message_parse_error(err, &e, nullptr);
        ADD_FAILURE() << "Cast audio pipeline error: " << (e ? e->message : "?");
        if (e) g_error_free(e);
        gst_message_unref(err);
    }
    gst_object_unref(bus);
    pipeline.stopCast();
}
