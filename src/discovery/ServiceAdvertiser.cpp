#include "discovery/ServiceAdvertiser.h"
#include "discovery/MdnsResponder.h"
#ifdef __linux__
#include "discovery/AvahiAdvertiser.h"
#elif defined(_WIN32) || defined(__APPLE__)
#include "discovery/BonjourAdvertiser.h"
#endif
#include <QByteArray>
#include <glib.h>

namespace airshow {

std::unique_ptr<ServiceAdvertiser> ServiceAdvertiser::create() {
    // AIRSHOW_MDNS=builtin skips the system mDNS service (testing, or a broken daemon).
    if (qgetenv("AIRSHOW_MDNS") != "builtin") {
#ifdef __linux__
        auto system = std::make_unique<AvahiAdvertiser>();
#elif defined(_WIN32) || defined(__APPLE__)
        auto system = std::make_unique<BonjourAdvertiser>();
#endif
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
        if (system->isAvailable()) return system;
        g_message("ServiceAdvertiser: no system mDNS service — using AirShow's built-in responder");
#endif
    }
    auto builtin = std::make_unique<MdnsResponder>();
    if (!builtin->isAvailable()) return nullptr;
    return builtin;
}

} // namespace airshow
