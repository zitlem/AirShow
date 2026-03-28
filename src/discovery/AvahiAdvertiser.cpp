#ifdef __linux__
#include "discovery/AvahiAdvertiser.h"
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>
#include <glib.h>

namespace myairshow {

AvahiAdvertiser::AvahiAdvertiser() {
    m_poll = avahi_threaded_poll_new();
    if (!m_poll) {
        g_critical("AvahiAdvertiser: failed to create threaded poll");
        return;
    }
    int err = 0;
    m_client = avahi_client_new(avahi_threaded_poll_get(m_poll),
                                AVAHI_CLIENT_NO_FAIL,
                                &AvahiAdvertiser::clientCallback,
                                this,
                                &err);
    if (!m_client) {
        g_critical("AvahiAdvertiser: avahi_client_new failed: %s",
                   avahi_strerror(err));
        return;
    }
    avahi_threaded_poll_start(m_poll);
}

AvahiAdvertiser::~AvahiAdvertiser() {
    stop();
}

bool AvahiAdvertiser::advertise(const std::string& serviceType,
                                const std::string& name,
                                uint16_t port,
                                const std::vector<TxtRecord>& txt) {
    if (!m_poll || !m_client) return false;
    avahi_threaded_poll_lock(m_poll);
    m_services.push_back({serviceType, name, port, txt});
    if (m_activeName.empty()) m_activeName = name;
    // If client is already running, register immediately.
    if (avahi_client_get_state(m_client) == AVAHI_CLIENT_S_RUNNING) {
        createServices(m_client);
    }
    avahi_threaded_poll_unlock(m_poll);
    return true;  // async — actual registration confirmed via group callback
}

bool AvahiAdvertiser::rename(const std::string& newName) {
    if (!m_poll || !m_client) return false;
    avahi_threaded_poll_lock(m_poll);
    m_activeName = newName;
    for (auto& svc : m_services) svc.name = newName;
    if (m_group) {
        avahi_entry_group_reset(m_group);
    }
    if (avahi_client_get_state(m_client) == AVAHI_CLIENT_S_RUNNING) {
        createServices(m_client);
    }
    avahi_threaded_poll_unlock(m_poll);
    return true;
}

bool AvahiAdvertiser::updateTxtRecord(const std::string& serviceType,
                                      const std::string& key,
                                      const std::string& value) {
    if (!m_poll || !m_client) return false;
    avahi_threaded_poll_lock(m_poll);

    // Find the service with the matching type and update the key in its TXT records.
    // If the key is not present, it is added.
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
        }
    }

    if (!found) {
        avahi_threaded_poll_unlock(m_poll);
        g_warning("AvahiAdvertiser::updateTxtRecord — service type '%s' not found",
                  serviceType.c_str());
        return false;
    }

    // Re-register ALL services with the updated TXT records.
    // This is acceptable since TXT updates are rare (e.g., once at startup when pk is known).
    if (m_group) {
        avahi_entry_group_reset(m_group);
    }
    if (avahi_client_get_state(m_client) == AVAHI_CLIENT_S_RUNNING) {
        createServices(m_client);
    }

    avahi_threaded_poll_unlock(m_poll);
    return true;
}

void AvahiAdvertiser::stop() {
    if (m_poll) {
        avahi_threaded_poll_stop(m_poll);
    }
    if (m_group) {
        avahi_entry_group_free(m_group);
        m_group = nullptr;
    }
    if (m_client) {
        avahi_client_free(m_client);
        m_client = nullptr;
    }
    if (m_poll) {
        avahi_threaded_poll_free(m_poll);
        m_poll = nullptr;
    }
}

// static
void AvahiAdvertiser::clientCallback(AvahiClient* client,
                                     AvahiClientState state,
                                     void* userdata) {
    auto* self = static_cast<AvahiAdvertiser*>(userdata);
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            self->createServices(client);
            break;
        case AVAHI_CLIENT_FAILURE:
            g_critical("AvahiAdvertiser: Avahi client failure: %s. "
                       "Is avahi-daemon running? Start it with: "
                       "sudo systemctl start avahi-daemon",
                       avahi_strerror(avahi_client_errno(client)));
            break;
        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:
            if (self->m_group) {
                avahi_entry_group_reset(self->m_group);
            }
            break;
        default:
            break;
    }
}

// static
void AvahiAdvertiser::groupCallback(AvahiEntryGroup* /*group*/,
                                    AvahiEntryGroupState state,
                                    void* userdata) {
    auto* self = static_cast<AvahiAdvertiser*>(userdata);
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            g_message("AvahiAdvertiser: services registered successfully");
            break;
        case AVAHI_ENTRY_GROUP_COLLISION: {
            // Two MyAirShow receivers on the same LAN — rename with suffix
            char* alt = avahi_alternative_service_name(self->m_activeName.c_str());
            g_warning("AvahiAdvertiser: name collision, using '%s'", alt);
            self->m_activeName = alt;
            avahi_free(alt);
            for (auto& svc : self->m_services) svc.name = self->m_activeName;
            if (self->m_group) avahi_entry_group_reset(self->m_group);
            self->createServices(self->m_client);
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE:
            g_critical("AvahiAdvertiser: entry group failure: %s",
                       avahi_strerror(avahi_client_errno(self->m_client)));
            break;
        default:
            break;
    }
}

void AvahiAdvertiser::createServices(AvahiClient* client) {
    if (m_services.empty()) return;

    if (!m_group) {
        m_group = avahi_entry_group_new(client,
                                        &AvahiAdvertiser::groupCallback,
                                        this);
        if (!m_group) {
            g_critical("AvahiAdvertiser: avahi_entry_group_new failed");
            return;
        }
    }

    for (const auto& svc : m_services) {
        AvahiStringList* txtList = buildTxtList(svc.txt);
        int ret = avahi_entry_group_add_service_strlst(
            m_group,
            AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC,
            static_cast<AvahiPublishFlags>(0),
            svc.name.c_str(),
            svc.type.c_str(),
            nullptr,   // domain (nullptr = .local)
            nullptr,   // host (nullptr = local hostname)
            svc.port,
            txtList
        );
        avahi_string_list_free(txtList);
        if (ret < 0) {
            g_critical("AvahiAdvertiser: failed to add service '%s' type '%s': %s",
                       svc.name.c_str(), svc.type.c_str(), avahi_strerror(ret));
            return;
        }
    }

    int ret = avahi_entry_group_commit(m_group);
    if (ret < 0) {
        g_critical("AvahiAdvertiser: avahi_entry_group_commit failed: %s",
                   avahi_strerror(ret));
    }
}

// static
AvahiStringList* AvahiAdvertiser::buildTxtList(const std::vector<TxtRecord>& txt) {
    AvahiStringList* list = nullptr;
    // Build in reverse order so iteration preserves original order
    for (auto it = txt.rbegin(); it != txt.rend(); ++it) {
        std::string entry = it->key + "=" + it->value;
        list = avahi_string_list_add(list, entry.c_str());
    }
    return list;
}

} // namespace myairshow
#endif // __linux__
