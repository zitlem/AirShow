#pragma once
#ifdef __linux__
#include "discovery/ServiceAdvertiser.h"
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/thread-watch.h>
#include <string>
#include <vector>

namespace myairshow {

// Linux mDNS backend using libavahi-client with AvahiThreadedPoll.
// Thread safety: all entry group operations are performed from within
// Avahi callbacks or with avahi_threaded_poll_lock() held.
class AvahiAdvertiser : public ServiceAdvertiser {
public:
    AvahiAdvertiser();
    ~AvahiAdvertiser() override;

    bool advertise(const std::string& serviceType,
                   const std::string& name,
                   uint16_t port,
                   const std::vector<TxtRecord>& txt) override;

    bool rename(const std::string& newName) override;
    void stop() override;
    bool updateTxtRecord(const std::string& serviceType,
                         const std::string& key,
                         const std::string& value) override;

private:
    // Avahi client callback — static, dispatches to instance methods.
    static void clientCallback(AvahiClient* client, AvahiClientState state,
                               void* userdata);

    // Entry group callback — handles COLLISION and FAILURE.
    static void groupCallback(AvahiEntryGroup* group, AvahiEntryGroupState state,
                              void* userdata);

    // Called from within Avahi thread to register all pending services.
    void createServices(AvahiClient* client);

    // Build AvahiStringList from TxtRecord vector.
    static AvahiStringList* buildTxtList(const std::vector<TxtRecord>& txt);

    struct ServiceRecord {
        std::string              type;
        std::string              name;
        uint16_t                 port;
        std::vector<TxtRecord>   txt;
    };

    AvahiThreadedPoll*           m_poll   = nullptr;
    AvahiClient*                 m_client = nullptr;
    AvahiEntryGroup*             m_group  = nullptr;
    std::vector<ServiceRecord>   m_services;
    std::string                  m_activeName;  // may differ from m_services[0].name after collision
};

} // namespace myairshow
#endif // __linux__
