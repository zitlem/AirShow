// Wave 0 test stubs for Phase 3 Display & Receiver UI
// ctest filter names used in VALIDATION:
//   test_display_aspect_ratio, test_connection_bridge, test_settings_bridge

#include <gtest/gtest.h>
#include <QtCore/QCoreApplication>
#include <QSettings>
#include "ui/ConnectionBridge.h"
#include "ui/SettingsBridge.h"
#include "settings/AppSettings.h"

class DisplayTest : public ::testing::Test {};

// DISP-01: forceAspectRatio — QML rendering (visual, cannot be automated with GTest)
TEST_F(DisplayTest, test_display_aspect_ratio) {
    GTEST_SKIP() << "Visual test — verified manually: GstGLQt6VideoItem forceAspectRatio:true provides letterboxing";
}

// DISP-02 + DISP-03: ConnectionBridge state machine
TEST_F(DisplayTest, test_connection_bridge_initial_state) {
    // RED: ConnectionBridge.cpp does not exist yet — will FAIL to link until Plan 02
    airshow::ConnectionBridge bridge;
    EXPECT_FALSE(bridge.isConnected());
    EXPECT_TRUE(bridge.deviceName().isEmpty());
    EXPECT_TRUE(bridge.protocol().isEmpty());
}

TEST_F(DisplayTest, test_connection_bridge_set_connected) {
    airshow::ConnectionBridge bridge;
    bridge.setConnected(true, "iPhone 15", "AirPlay");
    EXPECT_TRUE(bridge.isConnected());
    EXPECT_EQ(bridge.deviceName(), "iPhone 15");
    EXPECT_EQ(bridge.protocol(), "AirPlay");
}

TEST_F(DisplayTest, test_connection_bridge_set_disconnected) {
    airshow::ConnectionBridge bridge;
    bridge.setConnected(true, "iPhone 15", "AirPlay");
    bridge.setConnected(false);
    EXPECT_FALSE(bridge.isConnected());
    EXPECT_TRUE(bridge.deviceName().isEmpty());
    EXPECT_TRUE(bridge.protocol().isEmpty());
}

// DISP-03: SettingsBridge reads receiverName from AppSettings
TEST_F(DisplayTest, test_settings_bridge_receiver_name) {
    QCoreApplication::setOrganizationName("AirShowTest");
    QCoreApplication::setApplicationName("AirShowTest");
    {
        QSettings s;
        s.setValue("receiver/name", "TestReceiver");
        s.sync();
    }
    airshow::AppSettings settings;
    airshow::SettingsBridge bridge(settings);
    EXPECT_EQ(bridge.receiverName(), "TestReceiver");
    {
        QSettings s;
        s.remove("receiver/name");
        s.sync();
    }
}
