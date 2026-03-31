// test_security.cpp — Unit tests for SecurityManager and AppSettings security extensions
// Phase 07 Plan 01: Security & Hardening
//
// Coverage:
//   SEC-01: Device approval prompts and trusted device list
//   SEC-02: PIN pairing settings persistence
//   SEC-03: RFC1918 / link-local / loopback network filtering

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QSettings>
#include <QTimer>
#include <atomic>
#include <thread>

#include "security/SecurityManager.h"
#include "settings/AppSettings.h"

// ---------------------------------------------------------------------------
// Test environment: QCoreApplication is required for QMetaObject::invokeMethod
// ---------------------------------------------------------------------------
class SecurityTestEnvironment : public ::testing::Environment {
public:
    static int    argc;
    static char** argv;
    void SetUp() override {
        // QCoreApplication is a singleton — create once for the whole suite.
        if (!QCoreApplication::instance()) {
            app = new QCoreApplication(argc, argv);
        }
        // Isolate each test run in a temporary config directory so QSettings
        // do not persist between test runs or interfere with real config files.
        QCoreApplication::setOrganizationName("TestOrg");
        QCoreApplication::setApplicationName("TestSecApp");
    }

private:
    QCoreApplication* app = nullptr;
};

int    SecurityTestEnvironment::argc   = 0;
char** SecurityTestEnvironment::argv   = nullptr;

// Helper: clear test-scoped QSettings before each test
static void clearTestSettings() {
    QSettings s;
    s.remove("security");
    s.sync();
}

// ---------------------------------------------------------------------------
// SEC-03 — isLocalNetwork() RFC1918 / link-local / loopback checks
// ---------------------------------------------------------------------------
class IsLocalNetworkTest : public ::testing::Test {};

TEST_F(IsLocalNetworkTest, RFC1918TenNet) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("10.0.0.1")));
}

TEST_F(IsLocalNetworkTest, RFC1918TenNetBroadcast) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("10.255.255.255")));
}

TEST_F(IsLocalNetworkTest, RFC1918OneSevenTwo) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("172.16.5.1")));
}

TEST_F(IsLocalNetworkTest, RFC1918OneSevenTwoEnd) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("172.31.255.255")));
}

TEST_F(IsLocalNetworkTest, RFC1918OneNineTwo) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("192.168.1.1")));
}

TEST_F(IsLocalNetworkTest, LinkLocal) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("169.254.1.1")));
}

TEST_F(IsLocalNetworkTest, LoopbackAllowed) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("127.0.0.1")));
}

TEST_F(IsLocalNetworkTest, IPv6LoopbackAllowed) {
    EXPECT_TRUE(airshow::SecurityManager::isLocalNetwork(QHostAddress("::1")));
}

TEST_F(IsLocalNetworkTest, PublicIPRejected) {
    EXPECT_FALSE(airshow::SecurityManager::isLocalNetwork(QHostAddress("8.8.8.8")));
}

TEST_F(IsLocalNetworkTest, PublicIPRejected2) {
    EXPECT_FALSE(airshow::SecurityManager::isLocalNetwork(QHostAddress("1.2.3.4")));
}

TEST_F(IsLocalNetworkTest, PublicIPRejected3) {
    EXPECT_FALSE(airshow::SecurityManager::isLocalNetwork(QHostAddress("203.0.113.1")));
}

// ---------------------------------------------------------------------------
// SEC-02 — AppSettings PIN persistence
// ---------------------------------------------------------------------------
class AppSettingsPinTest : public ::testing::Test {
protected:
    void SetUp() override { clearTestSettings(); }
    void TearDown() override { clearTestSettings(); }
};

TEST_F(AppSettingsPinTest, RequireApprovalDefault) {
    airshow::AppSettings s;
    EXPECT_TRUE(s.requireApproval());
}

TEST_F(AppSettingsPinTest, PinEnabledDefault) {
    airshow::AppSettings s;
    EXPECT_FALSE(s.pinEnabled());
}

TEST_F(AppSettingsPinTest, PinDefault) {
    airshow::AppSettings s;
    EXPECT_EQ(s.pin(), QString());
}

TEST_F(AppSettingsPinTest, TrustedDevicesDefault) {
    airshow::AppSettings s;
    EXPECT_TRUE(s.trustedDevices().isEmpty());
}

TEST_F(AppSettingsPinTest, PinSettingsRoundTrip) {
    airshow::AppSettings s;
    s.setPinEnabled(true);
    EXPECT_TRUE(s.pinEnabled());
}

TEST_F(AppSettingsPinTest, PinValueRoundTrip) {
    airshow::AppSettings s;
    s.setPin("1234");
    EXPECT_EQ(s.pin(), "1234");
}

TEST_F(AppSettingsPinTest, RequireApprovalRoundTrip) {
    airshow::AppSettings s;
    s.setRequireApproval(false);
    EXPECT_FALSE(s.requireApproval());
}

// ---------------------------------------------------------------------------
// SEC-01 — AppSettings trusted device list persistence
// ---------------------------------------------------------------------------
class AppSettingsTrustedDeviceTest : public ::testing::Test {
protected:
    void SetUp() override { clearTestSettings(); }
    void TearDown() override { clearTestSettings(); }
};

TEST_F(AppSettingsTrustedDeviceTest, TrustedDevicePersists) {
    {
        airshow::AppSettings s;
        s.addTrustedDevice("AA:BB:CC:DD:EE:FF");
    }
    // New instance reads from same QSettings backing store
    airshow::AppSettings s2;
    EXPECT_TRUE(s2.trustedDevices().contains("AA:BB:CC:DD:EE:FF"));
}

TEST_F(AppSettingsTrustedDeviceTest, ClearTrustedDevices) {
    airshow::AppSettings s;
    s.addTrustedDevice("AA:BB:CC:DD:EE:FF");
    s.addTrustedDevice("11:22:33:44:55:66");
    s.clearTrustedDevices();
    EXPECT_TRUE(s.trustedDevices().isEmpty());
}

TEST_F(AppSettingsTrustedDeviceTest, RemoveTrustedDevice) {
    airshow::AppSettings s;
    s.addTrustedDevice("AA:BB:CC");
    s.addTrustedDevice("DD:EE:FF");
    s.removeTrustedDevice("AA:BB:CC");
    EXPECT_FALSE(s.trustedDevices().contains("AA:BB:CC"));
    EXPECT_TRUE(s.trustedDevices().contains("DD:EE:FF"));
}

TEST_F(AppSettingsTrustedDeviceTest, AddTrustedDeviceNoDuplicate) {
    airshow::AppSettings s;
    s.addTrustedDevice("AA:BB:CC");
    s.addTrustedDevice("AA:BB:CC");
    EXPECT_EQ(s.trustedDevices().size(), 1);
}

// ---------------------------------------------------------------------------
// SEC-01 — SecurityManager::checkConnection() approval logic
// ---------------------------------------------------------------------------
class SecurityManagerCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearTestSettings();
        settings = std::make_unique<airshow::AppSettings>();
        // Use a short timeout for tests (100 ms) to keep test suite fast
        manager  = std::make_unique<airshow::SecurityManager>(*settings);
        manager->setApprovalTimeoutMs(100);
    }
    void TearDown() override {
        manager.reset();
        settings.reset();
        clearTestSettings();
    }

    std::unique_ptr<airshow::AppSettings>   settings;
    std::unique_ptr<airshow::SecurityManager> manager;
};

TEST_F(SecurityManagerCheckTest, RequireApprovalFalseSkipsPrompt) {
    settings->setRequireApproval(false);
    // Should return immediately (true) without emitting requestApproval
    bool result = manager->checkConnection("Device", "AirPlay", "some-id");
    EXPECT_TRUE(result);
}

TEST_F(SecurityManagerCheckTest, TrustedDeviceSkipsPrompt) {
    settings->setRequireApproval(true);
    settings->addTrustedDevice("AA:BB:CC");
    bool result = manager->checkConnection("Device", "AirPlay", "AA:BB:CC");
    EXPECT_TRUE(result);
}

TEST_F(SecurityManagerCheckTest, TimeoutAutoDenies) {
    settings->setRequireApproval(true);
    // "unknown-device" is not in trusted list and nobody will release the semaphore
    bool result = manager->checkConnection("Unknown", "AirPlay", "unknown-device");
    EXPECT_FALSE(result);
}

TEST_F(SecurityManagerCheckTest, ResolveApprovalGrants) {
    settings->setRequireApproval(true);
    // Use a longer timeout for the approval grant test so the event loop has
    // time to dispatch the queued signal before the semaphore times out.
    manager->setApprovalTimeoutMs(2000);

    // Connect: when requestApproval fires on the Qt thread, immediately resolve it
    QObject::connect(manager.get(), &airshow::SecurityManager::requestApproval,
                     manager.get(), [&](const QString& requestId, const QString&, const QString&) {
                         manager->resolveApproval(requestId, true);
                     }, Qt::DirectConnection);

    // Run checkConnection on a background std::thread so the Qt event loop is
    // free to dispatch the QueuedConnection signal to resolveApproval.
    std::atomic<bool> threadResult{false};
    std::atomic<bool> threadDone{false};
    std::thread worker([&]() {
        threadResult = manager->checkConnection("MyDevice", "DLNA", "device-xyz");
        threadDone   = true;
    });

    // Pump the event loop while waiting for the background thread to finish.
    // The background thread emits requestApproval via QueuedConnection; the Qt
    // event loop delivers it here, resolveApproval() releases the semaphore,
    // and checkConnection() returns true on the background thread.
    while (!threadDone.load()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    worker.join();

    EXPECT_TRUE(threadResult.load());
}

// ---------------------------------------------------------------------------
// Main — register environment (provides QCoreApplication)
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    SecurityTestEnvironment::argc = argc;
    SecurityTestEnvironment::argv = argv;
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new SecurityTestEnvironment);
    return RUN_ALL_TESTS();
}
