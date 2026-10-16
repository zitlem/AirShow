#pragma once
#include <cstddef>
#include <cstdint>

// Time-window math for the precomputed Cast auth signature table in
// cast_auth_sigs.h. Kept separate so callers that only need to know whether
// Cast auth is available (e.g. DiscoveryManager) don't pull in the table.
//
// Each signature is valid for one 2-day window. The TLS peer certificate must
// use that window's start/end as notBefore/notAfter, so the cert has to be
// regenerated whenever the window changes.

namespace airshow::cast {

static constexpr size_t   kCastAuthSignatureCount = 795;
static constexpr size_t   kCastAuthSignatureSize  = 256;

// Start date for signature index calculation: 2023-08-15 00:00:00 UTC
static constexpr uint64_t kCastAuthStartEpoch = 1692057600ULL;

// Seconds per signature window (2 days)
static constexpr uint64_t kCastAuthWindowSeconds = 172800ULL;

// First second after the last window (~2027-12-21 00:00:00 UTC)
static constexpr uint64_t kCastAuthEndEpoch =
    kCastAuthStartEpoch + kCastAuthSignatureCount * kCastAuthWindowSeconds;

// Window index for a timestamp, or -1 if the timestamp is outside the table.
inline int64_t castAuthWindowIndex(uint64_t unixTimestamp) {
    if (unixTimestamp < kCastAuthStartEpoch || unixTimestamp >= kCastAuthEndEpoch)
        return -1;
    return static_cast<int64_t>((unixTimestamp - kCastAuthStartEpoch) / kCastAuthWindowSeconds);
}

inline bool isCastAuthAvailable(uint64_t unixTimestamp) {
    return castAuthWindowIndex(unixTimestamp) >= 0;
}

inline uint64_t castAuthWindowStart(int64_t windowIndex) {
    return kCastAuthStartEpoch + static_cast<uint64_t>(windowIndex) * kCastAuthWindowSeconds;
}

} // namespace airshow::cast
