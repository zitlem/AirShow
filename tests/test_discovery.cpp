// Wave 0 test stubs for Phase 2 Discovery & Protocol Abstraction
// Tests match VALIDATION.md per-task verification map.
// ctest -R filter names used in VALIDATION.md:
//   test_airplay_mdns, test_cast_mdns, test_dlna_ssdp, test_receiver_name, test_firewall

#include <gtest/gtest.h>
#include <QtCore/QCoreApplication>
#include <QSettings>
#include "settings/AppSettings.h"

#include <fstream>
#include <sstream>
#include "platform/WindowsFirewall.h"

class DiscoveryTest : public ::testing::Test {};

// DISC-01: AirPlay mDNS advertisement
TEST_F(DiscoveryTest, test_airplay_mdns) {
    GTEST_SKIP() << "Avahi backend integration test — requires avahi-daemon on LAN";
}

// DISC-02: Google Cast mDNS advertisement
TEST_F(DiscoveryTest, test_cast_mdns) {
    GTEST_SKIP() << "Avahi backend integration test — requires avahi-daemon on LAN";
}

// DISC-03: DLNA SSDP advertisement
TEST_F(DiscoveryTest, test_dlna_ssdp) {
    // Verify MediaRenderer.xml template exists and has correct DLNA device type
    // Full advertisement test requires libupnp at runtime — SKIPPED in CI
    const std::string xmlPath =
        std::string(BUILD_DIR) + "/resources/dlna/MediaRenderer.xml";
    std::ifstream xmlFile(xmlPath);
    if (!xmlFile.is_open()) {
        GTEST_SKIP() << "MediaRenderer.xml not found at " << xmlPath
                     << " — run cmake --build first";
    }
    std::ostringstream buf;
    buf << xmlFile.rdbuf();
    const std::string xml = buf.str();
    EXPECT_NE(xml.find("urn:schemas-upnp-org:device:MediaRenderer:1"),
              std::string::npos)
        << "MediaRenderer.xml must declare MediaRenderer:1 device type";
    EXPECT_NE(xml.find("AVTransport"), std::string::npos)
        << "MediaRenderer.xml must include AVTransport service";
    EXPECT_NE(xml.find("RenderingControl"), std::string::npos)
        << "MediaRenderer.xml must include RenderingControl service";
}

// DISC-04: Receiver name persistence via QSettings
TEST_F(DiscoveryTest, test_receiver_name) {
    // Use a temporary QSettings scope so test does not pollute real settings
    QCoreApplication::setOrganizationName("MyAirShowTest");
    QCoreApplication::setApplicationName("MyAirShowTest");
    {
        QSettings s;
        s.remove("receiver/name");  // ensure clean state
        s.sync();
    }

    myairshow::AppSettings settings;
    // Default should be the system hostname (not empty)
    QString defaultName = settings.receiverName();
    EXPECT_FALSE(defaultName.isEmpty()) << "Default receiver name must not be empty";

    // Set a custom name
    settings.setReceiverName("TestReceiver");
    EXPECT_EQ(settings.receiverName(), "TestReceiver");

    // Clean up
    {
        QSettings s;
        s.remove("receiver/name");
        s.sync();
    }
    QCoreApplication::setOrganizationName("MyAirShow");
    QCoreApplication::setApplicationName("MyAirShow");
}

// DISC-05: Windows firewall rule registration
TEST_F(DiscoveryTest, test_firewall) {
    // On Linux/macOS: registerRules() is a no-op that always returns true (D-14)
    // On Windows: requires COM/UAC — integration test only, skipped in CI
#ifdef _WIN32
    GTEST_SKIP() << "WindowsFirewall registerRules() requires COM/UAC on Windows — CI skip";
#else
    // Linux/macOS: verify no-op returns true
    EXPECT_TRUE(myairshow::WindowsFirewall::registerRules())
        << "WindowsFirewall::registerRules() must return true on Linux/macOS (D-14)";
    EXPECT_TRUE(myairshow::WindowsFirewall::rulesAlreadyRegistered())
        << "WindowsFirewall::rulesAlreadyRegistered() must return true on Linux/macOS (D-14)";
#endif
}
