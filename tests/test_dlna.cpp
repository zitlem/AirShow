#include <gtest/gtest.h>
#include "protocol/DlnaHandler.h"
#include "pipeline/MediaPipeline.h"
#include "ui/ConnectionBridge.h"
#include <gst/gst.h>
#include <QCoreApplication>

// Minimal QCoreApplication for Qt tests
// DlnaHandler is a QObject, needs a QCoreApplication instance
class DlnaTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static const char* argv[] = {"test_dlna"};
            static QCoreApplication app(argc, const_cast<char**>(argv));
        }
        // GStreamer init required for parseTimeString (uses GST_SECOND)
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
    }
};

// Register the test environment
testing::Environment* const env =
    testing::AddGlobalTestEnvironment(new DlnaTestEnvironment);

// ── DlnaHandler name and lifecycle tests ──────────────────────────────────────

TEST(DlnaHandlerTest, NameReturnsDlna) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    EXPECT_EQ(handler.name(), "dlna");
}

TEST(DlnaHandlerTest, NotRunningInitially) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    EXPECT_FALSE(handler.isRunning());
}

TEST(DlnaHandlerTest, StartReturnsTrueAndIsRunning) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());
    handler.stop();
}

TEST(DlnaHandlerTest, StopMakesNotRunning) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── Time parsing tests ────────────────────────────────────────────────────────

TEST(DlnaHandlerTest, ParseTimeStringBasic) {
    // "1:23:45" = 1*3600 + 23*60 + 45 = 5025 seconds
    gint64 expected = (gint64)(1 * 3600 + 23 * 60 + 45) * GST_SECOND;
    EXPECT_EQ(airshow::DlnaHandler::parseTimeString("1:23:45"), expected);
}

TEST(DlnaHandlerTest, ParseTimeStringZero) {
    EXPECT_EQ(airshow::DlnaHandler::parseTimeString("0:00:00"), (gint64)0);
}

TEST(DlnaHandlerTest, FormatGstTimeRoundTrip) {
    // 150 seconds = 0:02:30
    gint64 ns = (gint64)150 * GST_SECOND;
    std::string formatted = airshow::DlnaHandler::formatGstTime(ns);
    EXPECT_EQ(formatted, "0:02:30");
    // Round-trip: parse the formatted string back
    gint64 reparsed = airshow::DlnaHandler::parseTimeString(formatted);
    EXPECT_EQ(reparsed, ns);
}

// ── Integration tests: pipeline wiring and handler registration ───────────────

TEST(DlnaHandlerTest, SetPipelineNocrash) {
    // DlnaHandlerSetPipeline: setMediaPipeline must not crash with a valid pipeline ptr
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    airshow::MediaPipeline pipeline;
    // Should not crash or throw
    handler.setMediaPipeline(&pipeline);
    SUCCEED();
}

TEST(DlnaHandlerTest, StartStopWithPipeline) {
    // DlnaHandlerStartStop: handler is running after start, not running after stop
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    airshow::MediaPipeline pipeline;
    handler.setMediaPipeline(&pipeline);
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}
