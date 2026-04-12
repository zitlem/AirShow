#pragma once
#if defined(_WIN32) || defined(__APPLE__)
#include "discovery/ServiceAdvertiser.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

#ifdef __APPLE__
#include <dns_sd.h>
#else
// Windows: define dns_sd types locally to avoid requiring the Bonjour SDK.
// Functions are loaded at runtime from dnssd.dll (installed by iTunes/iCloud/Bonjour Print Services).
typedef struct _DNSServiceRef_t* DNSServiceRef;
typedef uint32_t DNSServiceFlags;
typedef int32_t  DNSServiceErrorType;
typedef uint32_t DNSRecordRef;

// Minimal TXTRecord helpers (re-implemented inline, no SDK needed)
struct TXTRecordRefInternal {
    std::vector<uint8_t> data;
};

enum {
    kDNSServiceErr_NoError           = 0,
    kDNSServiceErr_NameConflict      = -65548,
    kDNSServiceErr_ServiceNotRunning = -65563,
    kDNSServiceFlagsShareConnection  = 0x4000,
};

// Callback type matching dns_sd.h
#ifdef _WIN32
#define DNSSD_API __stdcall
#else
#define DNSSD_API
#endif

typedef void (DNSSD_API *DNSServiceRegisterReply)(
    DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
    const char* name, const char* regtype, const char* domain, void* context);
#endif // __APPLE__

namespace airshow {

// Windows and macOS mDNS backend.
// On macOS: links directly to dns_sd (system framework).
// On Windows: dynamically loads dnssd.dll at runtime (no Bonjour SDK needed to build).
//
// Thread model: a dedicated processing thread runs DNSServiceProcessResult()
// in a loop. All service registration mutations are protected by m_mutex.
class BonjourAdvertiser : public ServiceAdvertiser {
public:
    BonjourAdvertiser();
    ~BonjourAdvertiser() override;

    bool advertise(const std::string& serviceType,
                   const std::string& name,
                   uint16_t port,
                   const std::vector<TxtRecord>& txt) override;

    bool rename(const std::string& newName) override;
    void stop() override;
    bool updateTxtRecord(const std::string& serviceType,
                         const std::string& key,
                         const std::string& value) override;

    // Returns true if the Bonjour service is available on this system.
    bool isAvailable() const { return m_available; }

private:
    struct ServiceRecord {
        std::string              type;
        std::string              name;
        uint16_t                 port;
        std::vector<TxtRecord>   txt;
        DNSServiceRef            sdRef = nullptr;
    };

    // Build a TXT record byte vector from TxtRecord list.
    static std::vector<uint8_t> buildTxtData(const std::vector<TxtRecord>& txt);

    bool registerService(ServiceRecord& svc);
    void deregisterService(ServiceRecord& svc);
    void reregisterAll();
    void processThread();

#ifdef __APPLE__
    static void DNSSD_API registerCallback(
        DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
        const char* name, const char* regtype, const char* domain, void* context);
#else
    // Windows: function pointers loaded from dnssd.dll
    static void DNSSD_API registerCallback(
        DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
        const char* name, const char* regtype, const char* domain, void* context);

    bool loadBonjourDll();

    // DLL handle
    void* m_dll = nullptr;

    // Function pointers
    using FnCreateConnection = DNSServiceErrorType (DNSSD_API*)(DNSServiceRef*);
    using FnRegister = DNSServiceErrorType (DNSSD_API*)(
        DNSServiceRef*, DNSServiceFlags, uint32_t, const char*, const char*,
        const char*, const char*, uint16_t, uint16_t, const void*,
        DNSServiceRegisterReply, void*);
    using FnUpdateRecord = DNSServiceErrorType (DNSSD_API*)(
        DNSServiceRef, DNSRecordRef, uint32_t, uint16_t, const void*, uint32_t);
    using FnRefSockFD = int (DNSSD_API*)(DNSServiceRef);
    using FnProcessResult = DNSServiceErrorType (DNSSD_API*)(DNSServiceRef);
    using FnRefDeallocate = void (DNSSD_API*)(DNSServiceRef);

    FnCreateConnection  m_fnCreateConnection  = nullptr;
    FnRegister          m_fnRegister          = nullptr;
    FnUpdateRecord      m_fnUpdateRecord      = nullptr;
    FnRefSockFD         m_fnRefSockFD         = nullptr;
    FnProcessResult     m_fnProcessResult     = nullptr;
    FnRefDeallocate     m_fnRefDeallocate     = nullptr;
#endif

    std::mutex                   m_mutex;
    std::vector<ServiceRecord>   m_services;
    std::string                  m_activeName;
    DNSServiceRef                m_sharedConnection = nullptr;
    std::thread                  m_thread;
    std::atomic<bool>            m_running{false};
    bool                         m_available = false;
};

} // namespace airshow
#endif // _WIN32 || __APPLE__
