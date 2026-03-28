#include "discovery/ServiceAdvertiser.h"
#ifdef __linux__
#include "discovery/AvahiAdvertiser.h"
#endif

namespace myairshow {

std::unique_ptr<ServiceAdvertiser> ServiceAdvertiser::create() {
#ifdef __linux__
    return std::make_unique<AvahiAdvertiser>();
#elif defined(__APPLE__)
    // DnsSdAdvertiser — implemented in Phase 2 macOS port (future)
    return nullptr;
#elif defined(_WIN32)
    // BonjourAdvertiser — implemented in Phase 2 Windows port (future)
    return nullptr;
#else
    return nullptr;
#endif
}

} // namespace myairshow
