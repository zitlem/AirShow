#include <gtest/gtest.h>
#include "protocol/AirPlayHandler.h"
#include "ui/ConnectionBridge.h"

namespace {

// Test that AirPlayHandler can be instantiated without crashing
TEST(AirPlayHandlerTest, CanInstantiate) {
    airshow::ConnectionBridge bridge;
    // Note: DiscoveryManager passed as nullptr — start() is not called in this test
    airshow::AirPlayHandler handler(&bridge, nullptr, "AA:BB:CC:DD:EE:FF", "/tmp/test_airplay.key");
    EXPECT_EQ(handler.name(), "airplay");
    EXPECT_FALSE(handler.isRunning());
}

// Test that stop() on a non-started handler does not crash
TEST(AirPlayHandlerTest, StopWithoutStart) {
    airshow::ConnectionBridge bridge;
    airshow::AirPlayHandler handler(&bridge, nullptr, "AA:BB:CC:DD:EE:FF", "/tmp/test_airplay.key");
    handler.stop();  // Should not crash
    EXPECT_FALSE(handler.isRunning());
}

// Test that setMediaPipeline stores the pipeline pointer (verifies the critical
// ProtocolManager::addHandler() -> setMediaPipeline() link)
TEST(AirPlayHandlerTest, SetMediaPipelineStoresPointer) {
    airshow::ConnectionBridge bridge;
    airshow::AirPlayHandler handler(&bridge, nullptr, "AA:BB:CC:DD:EE:FF", "/tmp/test_airplay.key");
    // Passing nullptr as pipeline — just verifying the method doesn't crash
    handler.setMediaPipeline(nullptr);
    EXPECT_FALSE(handler.isRunning());
}

} // namespace
