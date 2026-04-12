#if defined(_WIN32) || defined(__APPLE__)
#include "discovery/BonjourAdvertiser.h"
#include <glib.h>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>  // htons, select, fd_set
#include <windows.h>   // LoadLibrary, GetProcAddress
#else
#include <arpa/inet.h> // htons
#include <sys/select.h>
#endif

namespace airshow {

// ── TXT record helper ──────────────────────────────────────────────────────────
// Build a DNS-SD TXT record byte vector. Format: each entry is
// <length_byte><key=value>. Total must be <= 65535 bytes.
std::vector<uint8_t> BonjourAdvertiser::buildTxtData(const std::vector<TxtRecord>& txt) {
    std::vector<uint8_t> data;
    for (const auto& rec : txt) {
        std::string entry = rec.key + "=" + rec.value;
        if (entry.size() > 255) continue; // DNS-SD limit per entry
        data.push_back(static_cast<uint8_t>(entry.size()));
        data.insert(data.end(), entry.begin(), entry.end());
    }
    if (data.empty()) {
        data.push_back(0); // Empty TXT record is a single zero byte
    }
    return data;
}

// ── Constructor / Destructor ───────────────────────────────────────────────────

BonjourAdvertiser::BonjourAdvertiser() {
#ifdef _WIN32
    if (!loadBonjourDll()) {
        g_warning("BonjourAdvertiser: dnssd.dll not found. "
                  "Install iTunes, iCloud, or Bonjour Print Services to enable mDNS discovery.");
        return;
    }

    DNSServiceErrorType err = m_fnCreateConnection(&m_sharedConnection);
#else
    DNSServiceErrorType err = DNSServiceCreateConnection(&m_sharedConnection);
#endif

    if (err != kDNSServiceErr_NoError) {
        g_critical("BonjourAdvertiser: DNSServiceCreateConnection failed (error %d). "
                   "Is Bonjour/mDNSResponder running?", err);
        m_sharedConnection = nullptr;
        return;
    }

    m_available = true;
    m_running = true;
    m_thread = std::thread(&BonjourAdvertiser::processThread, this);
}

BonjourAdvertiser::~BonjourAdvertiser() {
    stop();
#ifdef _WIN32
    if (m_dll) {
        FreeLibrary(static_cast<HMODULE>(m_dll));
        m_dll = nullptr;
    }
#endif
}

// ── Public API ─────────────────────────────────────────────────────────────────

bool BonjourAdvertiser::advertise(const std::string& serviceType,
                                   const std::string& name,
                                   uint16_t port,
                                   const std::vector<TxtRecord>& txt) {
    if (!m_available) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    m_services.push_back({serviceType, name, port, txt, nullptr});
    if (m_activeName.empty()) m_activeName = name;

    return registerService(m_services.back());
}

bool BonjourAdvertiser::rename(const std::string& newName) {
    if (!m_available) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeName = newName;
    for (auto& svc : m_services) {
        svc.name = newName;
    }
    reregisterAll();
    return true;
}

bool BonjourAdvertiser::updateTxtRecord(const std::string& serviceType,
                                         const std::string& key,
                                         const std::string& value) {
    if (!m_available) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    bool found = false;
    for (auto& svc : m_services) {
        if (svc.type == serviceType) {
            found = true;
            bool updated = false;
            for (auto& rec : svc.txt) {
                if (rec.key == key) {
                    rec.value = value;
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                svc.txt.push_back({key, value});
            }

            // Update TXT record on the live registration
            if (svc.sdRef) {
                auto txtData = buildTxtData(svc.txt);
#ifdef _WIN32
                DNSServiceErrorType err = m_fnUpdateRecord(
                    svc.sdRef, nullptr, 0,
                    static_cast<uint16_t>(txtData.size()), txtData.data(), 0);
#else
                // macOS: use TXTRecordRef API
                TXTRecordRef txtRef;
                TXTRecordCreate(&txtRef, 0, nullptr);
                for (const auto& r : svc.txt) {
                    TXTRecordSetValue(&txtRef, r.key.c_str(),
                                      static_cast<uint8_t>(r.value.size()),
                                      r.value.c_str());
                }
                DNSServiceErrorType err = DNSServiceUpdateRecord(
                    svc.sdRef, nullptr, 0,
                    TXTRecordGetLength(&txtRef),
                    TXTRecordGetBytesPtr(&txtRef), 0);
                TXTRecordDeallocate(&txtRef);
#endif
                if (err != kDNSServiceErr_NoError) {
                    g_warning("BonjourAdvertiser: TXT update failed for '%s' (error %d), re-registering",
                              serviceType.c_str(), err);
                    deregisterService(svc);
                    registerService(svc);
                }
            }
        }
    }

    if (!found) {
        g_warning("BonjourAdvertiser::updateTxtRecord — service type '%s' not found",
                  serviceType.c_str());
        return false;
    }
    return true;
}

void BonjourAdvertiser::stop() {
    m_running = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& svc : m_services) {
            deregisterService(svc);
        }
        m_services.clear();
    }

    if (m_sharedConnection) {
#ifdef _WIN32
        if (m_fnRefDeallocate) m_fnRefDeallocate(m_sharedConnection);
#else
        DNSServiceRefDeallocate(m_sharedConnection);
#endif
        m_sharedConnection = nullptr;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_available = false;
}

// ── Private implementation ─────────────────────────────────────────────────────

bool BonjourAdvertiser::registerService(ServiceRecord& svc) {
    auto txtData = buildTxtData(svc.txt);

    svc.sdRef = m_sharedConnection;

#ifdef _WIN32
    DNSServiceErrorType err = m_fnRegister(
        &svc.sdRef,
        kDNSServiceFlagsShareConnection,
        0,                           // all interfaces
        svc.name.c_str(),
        svc.type.c_str(),
        nullptr,                     // domain (.local default)
        nullptr,                     // host (local machine)
        htons(svc.port),
        static_cast<uint16_t>(txtData.size()),
        txtData.data(),
        &BonjourAdvertiser::registerCallback,
        this
    );
#else
    TXTRecordRef txtRef;
    TXTRecordCreate(&txtRef, 0, nullptr);
    for (const auto& r : svc.txt) {
        TXTRecordSetValue(&txtRef, r.key.c_str(),
                          static_cast<uint8_t>(r.value.size()),
                          r.value.c_str());
    }

    DNSServiceErrorType err = DNSServiceRegister(
        &svc.sdRef,
        kDNSServiceFlagsShareConnection,
        0,
        svc.name.c_str(),
        svc.type.c_str(),
        nullptr,
        nullptr,
        htons(svc.port),
        TXTRecordGetLength(&txtRef),
        TXTRecordGetBytesPtr(&txtRef),
        &BonjourAdvertiser::registerCallback,
        this
    );
    TXTRecordDeallocate(&txtRef);
#endif

    if (err != kDNSServiceErr_NoError) {
        g_critical("BonjourAdvertiser: register failed for '%s' type '%s' (error %d)",
                   svc.name.c_str(), svc.type.c_str(), err);
        svc.sdRef = nullptr;
        return false;
    }

    g_message("BonjourAdvertiser: registered '%s' type '%s' on port %u",
              svc.name.c_str(), svc.type.c_str(), svc.port);
    return true;
}

void BonjourAdvertiser::deregisterService(ServiceRecord& svc) {
    if (svc.sdRef && svc.sdRef != m_sharedConnection) {
#ifdef _WIN32
        if (m_fnRefDeallocate) m_fnRefDeallocate(svc.sdRef);
#else
        DNSServiceRefDeallocate(svc.sdRef);
#endif
    }
    svc.sdRef = nullptr;
}

void BonjourAdvertiser::reregisterAll() {
    for (auto& svc : m_services) {
        deregisterService(svc);
        registerService(svc);
    }
}

void BonjourAdvertiser::processThread() {
    while (m_running && m_sharedConnection) {
        int fd;
#ifdef _WIN32
        fd = m_fnRefSockFD(m_sharedConnection);
#else
        fd = DNSServiceRefSockFD(m_sharedConnection);
#endif
        if (fd < 0) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int result = select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (result > 0 && FD_ISSET(fd, &readfds)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            DNSServiceErrorType err;
#ifdef _WIN32
            err = m_fnProcessResult(m_sharedConnection);
#else
            err = DNSServiceProcessResult(m_sharedConnection);
#endif
            if (err != kDNSServiceErr_NoError) {
                g_warning("BonjourAdvertiser: ProcessResult error %d", err);
                if (err == kDNSServiceErr_ServiceNotRunning) {
                    g_critical("BonjourAdvertiser: Bonjour service not running — stopping");
                    break;
                }
            }
        }
    }
}

// static
void DNSSD_API BonjourAdvertiser::registerCallback(
    DNSServiceRef /*sdRef*/,
    DNSServiceFlags /*flags*/,
    DNSServiceErrorType errorCode,
    const char* name,
    const char* regtype,
    const char* domain,
    void* /*context*/)
{
    if (errorCode == kDNSServiceErr_NoError) {
        g_message("BonjourAdvertiser: service registered: %s.%s%s",
                  name, regtype, domain);
    } else if (errorCode == kDNSServiceErr_NameConflict) {
        g_warning("BonjourAdvertiser: name conflict for '%s' — Bonjour will auto-rename",
                  name);
    } else {
        g_warning("BonjourAdvertiser: registration callback error %d for '%s'",
                  errorCode, name);
    }
}

// ── Windows DLL loading ────────────────────────────────────────────────────────

#ifdef _WIN32
bool BonjourAdvertiser::loadBonjourDll() {
    // Try loading dnssd.dll — installed by iTunes, iCloud, or Bonjour Print Services
    HMODULE dll = LoadLibraryA("dnssd.dll");
    if (!dll) {
        // Also try the Bonjour SDK path
        dll = LoadLibraryA("C:\\Program Files\\Bonjour SDK\\Bin\\Win64\\dnssd.dll");
    }
    if (!dll) {
        return false;
    }

    m_dll = dll;

    m_fnCreateConnection = reinterpret_cast<FnCreateConnection>(
        GetProcAddress(dll, "DNSServiceCreateConnection"));
    m_fnRegister = reinterpret_cast<FnRegister>(
        GetProcAddress(dll, "DNSServiceRegister"));
    m_fnUpdateRecord = reinterpret_cast<FnUpdateRecord>(
        GetProcAddress(dll, "DNSServiceUpdateRecord"));
    m_fnRefSockFD = reinterpret_cast<FnRefSockFD>(
        GetProcAddress(dll, "DNSServiceRefSockFD"));
    m_fnProcessResult = reinterpret_cast<FnProcessResult>(
        GetProcAddress(dll, "DNSServiceProcessResult"));
    m_fnRefDeallocate = reinterpret_cast<FnRefDeallocate>(
        GetProcAddress(dll, "DNSServiceRefDeallocate"));

    if (!m_fnCreateConnection || !m_fnRegister || !m_fnRefSockFD ||
        !m_fnProcessResult || !m_fnRefDeallocate) {
        g_warning("BonjourAdvertiser: dnssd.dll loaded but missing required functions");
        FreeLibrary(dll);
        m_dll = nullptr;
        return false;
    }

    g_message("BonjourAdvertiser: dnssd.dll loaded successfully");
    return true;
}
#endif

} // namespace airshow
#endif // _WIN32 || __APPLE__
