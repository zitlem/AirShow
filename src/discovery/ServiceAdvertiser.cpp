#include "discovery/ServiceAdvertiser.h"
#ifdef __linux__
#include "discovery/AvahiAdvertiser.h"
#elif defined(_WIN32) || defined(__APPLE__)
#include "discovery/BonjourAdvertiser.h"
#endif

namespace airshow {

std::unique_ptr<ServiceAdvertiser> ServiceAdvertiser::create() {
#ifdef __linux__
    return std::make_unique<AvahiAdvertiser>();
#elif defined(_WIN32) || defined(__APPLE__)
    return std::make_unique<BonjourAdvertiser>();
#else
    return nullptr;
#endif
}

} // namespace airshow
