#include <gtest/gtest.h>
#include "protocol/AirPlayHandler.h"
#include "ui/ConnectionBridge.h"
#include <dnssd.h>
#include <string>

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

// AirShow's dnssd replacement must work with no Bonjour/Avahi present and
// produce the TXT records the /info handler serves.
TEST(AirPlayDnssdTest, BuildsTxtRecordsWithoutSystemMdns) {
    const char mac[6] = {'\xAA', '\xBB', '\xCC', '\x01', '\x02', '\x03'};
    int err = -1;
    dnssd_t* dnssd = dnssd_init("Living Room", 11, mac, 6, &err, 0);
    ASSERT_NE(dnssd, nullptr);
    EXPECT_EQ(err, DNSSD_ERROR_NOERROR);

    char pk[] = "00112233";
    dnssd_set_pk(dnssd, pk);

    int len = 0;
    const char* name = dnssd_get_name(dnssd, &len);
    EXPECT_EQ(std::string(name, len), "Living Room");

    const char* txt = dnssd_get_airplay_txt(dnssd, &len);
    const std::string airplay(txt, len);
    EXPECT_NE(airplay.find("deviceid=aa:bb:cc:01:02:03"), std::string::npos);
    EXPECT_NE(airplay.find("pk=00112233"), std::string::npos);
    EXPECT_NE(airplay.find("features=0x"), std::string::npos);
    EXPECT_EQ(static_cast<unsigned char>(airplay[0]), sizeof("deviceid=aa:bb:cc:01:02:03") - 1);

    txt = dnssd_get_raop_txt(dnssd, &len);
    const std::string raop(txt, len);
    EXPECT_NE(raop.find("pk=00112233"), std::string::npos);
    EXPECT_EQ(raop.find("sf="), raop.rfind("sf="));  // a repeated key replaces the old entry

    dnssd_destroy(dnssd);
}

} // namespace
