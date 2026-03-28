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

// FOUND-02: GStreamer pipeline renders videotestsrc frames
// Stub: verifies GStreamer is initialised. Plan 02 will replace this body.
TEST_F(PipelineTest, test_video_pipeline) {
    EXPECT_TRUE(gst_is_initialized())
        << "GStreamer must be initialised before pipeline tests";
    // TODO (Plan 02): Construct MediaPipeline, call init(nullptr),
    // set state to PLAYING, assert GST_STATE_PLAYING, check no ERROR bus msg
    GTEST_SKIP() << "Stub — implemented in Plan 02";
}

// FOUND-03: audiotestsrc plays through autoaudiosink
// Stub: verifies avdec_h264 plugin is available as a canary for gst-libav install.
TEST_F(PipelineTest, test_audio_pipeline) {
    EXPECT_TRUE(gst_is_initialized());
    // TODO (Plan 02): Construct MediaPipeline, call init(nullptr),
    // assert pipeline enters PLAYING state within 2s, check no ERROR bus msg
    GTEST_SKIP() << "Stub — implemented in Plan 02";
}

// FOUND-04: Mute toggle sets volume to 0; unmute restores to 1.0
TEST_F(PipelineTest, test_mute_toggle) {
    myairshow::MediaPipeline pipeline;
    // Stub body: just verify isMuted() default is false
    EXPECT_FALSE(pipeline.isMuted());
    // TODO (Plan 02): call pipeline.setMuted(true); EXPECT_TRUE(pipeline.isMuted())
    //                 call pipeline.setMuted(false); EXPECT_FALSE(pipeline.isMuted())
    GTEST_SKIP() << "Stub — full impl in Plan 02";
}

// FOUND-05: decodebin selects a decoder and logs its name
TEST_F(PipelineTest, test_decoder_detection) {
    EXPECT_TRUE(gst_is_initialized());
    // TODO (Plan 03): Build videotestsrc ! x264enc ! decodebin pipeline,
    // register element-added callback, enter PLAYING state,
    // assert activeDecoder() is populated with a name matching known decoders
    GTEST_SKIP() << "Stub — implemented in Plan 03";
}

// Smoke test: verifies required GStreamer plugins are present
TEST(SmokeTest, required_plugins_available) {
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
