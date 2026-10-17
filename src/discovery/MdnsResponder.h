#pragma once
#include "discovery/ServiceAdvertiser.h"
#include <cstdint>
#include <string>
#include <vector>

class QThread;

namespace airshow {

class MdnsResponderCore;

// AirShow's own mDNS/DNS-SD responder (RFC 6762/6763), used when the system has
// no mDNS service of its own — Windows without Apple's Bonjour, or Linux
// without avahi-daemon. It answers queries for our services on 224.0.0.251:5353
// (IPv4), probes for name conflicts (renaming "Name" to "Name (2)" on a clash),
// announces new and changed records and sends goodbyes when stopping.
//
// Networking runs on a private thread; every public method is thread-safe and
// must not be called from that thread.
class MdnsResponder : public ServiceAdvertiser {
public:
    // port is 5353 except in tests.
    explicit MdnsResponder(uint16_t port = 5353);
    ~MdnsResponder() override;

    // False if the mDNS port could not be opened.
    bool isAvailable() const { return m_available; }

    bool advertise(const std::string& serviceType,
                   const std::string& name,
                   uint16_t port,
                   const std::vector<TxtRecord>& txt) override;
    bool rename(const std::string& newName) override;
    void stop() override;
    bool updateTxtRecord(const std::string& serviceType,
                         const std::string& key,
                         const std::string& value) override;

    // Host name the SRV records point at, e.g. "DESKTOP-1234-AirShow.local".
    std::string hostName() const;
    // Instance names currently advertised for a service type — differs from
    // the requested name after a conflict rename.
    std::vector<std::string> instanceNames(const std::string& serviceType) const;

private:
    QThread*           m_thread = nullptr;
    MdnsResponderCore* m_core   = nullptr;  // lives on m_thread
    bool               m_available = false;
};

} // namespace airshow
