// Wave 0 test stubs for Phase 2 Discovery & Protocol Abstraction
// Tests match VALIDATION.md per-task verification map.
// ctest -R filter names used in VALIDATION.md:
//   test_airplay_mdns, test_cast_mdns, test_dlna_ssdp, test_receiver_name, test_firewall

#include <gtest/gtest.h>

class DiscoveryTest : public ::testing::Test {};

// DISC-01: AirPlay mDNS advertisement
TEST_F(DiscoveryTest, test_airplay_mdns) {
    GTEST_SKIP() << "Avahi backend not yet implemented — Plan 02";
}

// DISC-02: Google Cast mDNS advertisement
TEST_F(DiscoveryTest, test_cast_mdns) {
    GTEST_SKIP() << "Avahi backend not yet implemented — Plan 02";
}

// DISC-03: DLNA SSDP advertisement
TEST_F(DiscoveryTest, test_dlna_ssdp) {
    GTEST_SKIP() << "UpnpAdvertiser not yet implemented — Plan 03";
}

// DISC-04: Receiver name persistence
TEST_F(DiscoveryTest, test_receiver_name) {
    GTEST_SKIP() << "AppSettings not yet wired — Plan 02";
}

// DISC-05: Windows firewall rule registration
TEST_F(DiscoveryTest, test_firewall) {
    GTEST_SKIP() << "WindowsFirewall is Windows-only — Plan 03";
}
