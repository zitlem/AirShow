// Wave 0 test stubs for Phase 2 Discovery & Protocol Abstraction
// Tests match VALIDATION.md per-task verification map.
// ctest -R filter names used in VALIDATION.md:
//   test_airplay_mdns, test_cast_mdns, test_dlna_ssdp, test_receiver_name, test_firewall

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include "settings/AppSettings.h"

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
    GTEST_SKIP() << "UpnpAdvertiser not yet implemented — Plan 03";
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
    GTEST_SKIP() << "WindowsFirewall is Windows-only — Plan 03";
}
