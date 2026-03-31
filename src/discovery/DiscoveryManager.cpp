#include "discovery/DiscoveryManager.h"
#include "discovery/ServiceAdvertiser.h"
#include "settings/AppSettings.h"
#include <QSettings>
#include <QUuid>
#include <glib.h>
#include <algorithm>

// Platform headers for MAC address reading
#ifdef __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#endif

namespace airshow {

static constexpr uint16_t kAirPlayPort   = 7000;
static constexpr uint16_t kCastPort      = 8009;
static constexpr uint16_t kMiracastPort  = 7250;  // MS-MICE control port (D-04)

DiscoveryManager::DiscoveryManager(AppSettings* settings)
    : m_settings(settings)
    , m_advertiser(ServiceAdvertiser::create()) {}

DiscoveryManager::~DiscoveryManager() {
    stop();
}

bool DiscoveryManager::start() {
    if (!m_advertiser) {
        g_warning("DiscoveryManager: no ServiceAdvertiser available on this platform");
        return false;
    }

    const std::string name = m_settings->receiverName().toStdString();
    const std::string mac  = readMacAddress();
    const std::string pi   = getOrCreateUuid("airplay/pi");
    const std::string castId = getOrCreateUuid("cast/id");

    // Placeholder public key: 64 zero bytes as hex (Phase 4 will replace with real key)
    const std::string pkPlaceholder(128, '0');

    // --- _airplay._tcp (DISC-01) ---
    std::vector<TxtRecord> airplayTxt = {
        {"deviceid", mac},
        {"features", "0x5A7FFEE6,0x1E"},
        {"model",    "AppleTV3,2"},
        {"srcvers",  "220.68"},
        {"pk",       pkPlaceholder},
        {"pi",       pi},
    };
    m_advertiser->advertise("_airplay._tcp", name, kAirPlayPort, airplayTxt);

    // --- _raop._tcp (DISC-01 — iOS audio/legacy discovery) ---
    // Service name format for RAOP: "<MAC>@<ReceiverName>"
    const std::string raopName = mac + "@" + name;
    std::vector<TxtRecord> raopTxt = {
        {"txtvers", "1"},
        {"ch",      "2"},
        {"cn",      "0,1,2,3"},
        {"da",      "true"},
        {"et",      "0,3,5"},
        {"md",      "0,1,2"},
        {"pw",      "false"},
        {"sr",      "44100"},
        {"ss",      "16"},
        {"tp",      "UDP"},
        {"vn",      "65537"},
        {"vs",      "220.68"},
        {"am",      "AppleTV3,2"},
        {"pk",      pkPlaceholder},
        {"ft",      "0x5A7FFEE6,0x1E"},
    };
    m_advertiser->advertise("_raop._tcp", raopName, kAirPlayPort, raopTxt);

    // --- _googlecast._tcp (DISC-02) ---
    // Remove hyphens from UUID for Cast id field
    std::string castIdNoHyphens = castId;
    castIdNoHyphens.erase(
        std::remove(castIdNoHyphens.begin(), castIdNoHyphens.end(), '-'),
        castIdNoHyphens.end()
    );
    std::vector<TxtRecord> castTxt = {
        {"id", castIdNoHyphens},
        {"ve", "02"},
        {"md", "AirShow"},
        {"fn", name},
        {"ic", "/icon.png"},
        {"ca", "5"},
        {"st", "0"},
        {"rs", ""},
    };
    m_advertiser->advertise("_googlecast._tcp", name, kCastPort, castTxt);

    // --- _display._tcp (MS-MICE Miracast over Infrastructure — DISC-04, D-04) ---
    // Windows "Connect" app discovers receivers via this mDNS service type.
    // TXT records: VerMgmt and VerMin from MS-MICE spec revision 6.0.
    // Per RESEARCH.md Pitfall 1: without this advertisement Windows "Connect" shows no devices.
    std::vector<TxtRecord> miracastTxt = {
        {"VerMgmt", "0x0202"},  // MS-MICE management version (revision 6.0)
        {"VerMin",  "0x0100"},  // MS-MICE minimum version
    };
    m_advertiser->advertise("_display._tcp", name, kMiracastPort, miracastTxt);

    m_running = true;
    g_message("DiscoveryManager: started advertising as '%s'", name.c_str());
    return true;
}

void DiscoveryManager::stop() {
    if (m_advertiser && m_running) {
        m_advertiser->stop();
    }
    m_running = false;
}

void DiscoveryManager::rename(const std::string& newName) {
    if (m_advertiser && m_running) {
        m_advertiser->rename(newName);
        g_message("DiscoveryManager: re-registered as '%s'", newName.c_str());
    }
}

bool DiscoveryManager::updateTxtRecord(const std::string& serviceType,
                                       const std::string& key,
                                       const std::string& value) {
    if (!m_advertiser || !m_running) {
        g_warning("DiscoveryManager::updateTxtRecord — advertiser not running");
        return false;
    }
    return m_advertiser->updateTxtRecord(serviceType, key, value);
}

bool DiscoveryManager::isRunning() const {
    return m_running;
}

std::string DiscoveryManager::deviceId() const {
    return readMacAddress();
}

// static
std::string DiscoveryManager::readMacAddress() {
#ifdef __linux__
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "AA:BB:CC:DD:EE:FF";

    struct ifconf ifc{};
    char buf[4096]{};
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
        close(sock);
        return "AA:BB:CC:DD:EE:FF";
    }

    struct ifreq* ifr = ifc.ifc_req;
    int numIfaces = ifc.ifc_len / static_cast<int>(sizeof(struct ifreq));
    for (int i = 0; i < numIfaces; ++i) {
        if (std::string(ifr[i].ifr_name) == "lo") continue;
        struct ifreq req{};
        std::strncpy(req.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFHWADDR, &req) == 0) {
            close(sock);
            unsigned char* mac =
                reinterpret_cast<unsigned char*>(req.ifr_hwaddr.sa_data);
            char result[18];
            snprintf(result, sizeof(result),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return result;
        }
    }
    close(sock);
#endif
    return "AA:BB:CC:DD:EE:FF";
}

// static
std::string DiscoveryManager::getOrCreateUuid(const std::string& settingsKey) {
    QSettings s;
    QString key = QString::fromStdString(settingsKey);
    if (!s.contains(key)) {
        QString newUuid = QUuid::createUuid().toString(QUuid::WithBraces);
        s.setValue(key, newUuid);
        s.sync();
    }
    return s.value(key).toString().toStdString();
}

} // namespace airshow
