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
#include <set>
#include <string>
#include "pipeline/MediaPipeline.h"
#include "pipeline/DecoderInfo.h"

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
    airshow::MediaPipeline pipeline;
    // init with nullptr qmlVideoItem — qml6glsink logs a warning but must not crash
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

    // Register callback to capture decoder name
    std::string capturedDecoderName;
    airshow::DecoderType capturedType = airshow::DecoderType::Software;
    bool callbackFired = false;

    pipeline.setDecoderSelectedCallback(
        [&](const airshow::DecoderInfo& info) {
            capturedDecoderName = info.elementName;
            capturedType        = info.type;
            callbackFired       = true;
        });

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

    const std::string& name = pipeline.activeDecoder()->elementName;
    EXPECT_FALSE(name.empty()) << "Decoder element name must not be empty";

    // Verify the name is a known decoder (D-11 list + software fallback D-12)
    static const std::set<std::string> knownDecoders = {
        "vaapih264dec",   // Linux VAAPI
        "vaaph264dec",    // alternative VAAPI name
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
        {"qml6glsink",    "gstreamer1.0-qt6"},
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
