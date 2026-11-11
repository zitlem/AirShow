#pragma once
#include "cast/CastAuth.h"

#include <optional>

namespace airshow {

// The Cast auth credentials AirShow uses, loaded once and cached. Looked up in order:
//   1. $AIRSHOW_CAST_AUTH (path to a cast-auth.json)
//   2. cast-auth.json in the per-user app data directory
//   3. the copy built into the binary (resources/cast/cast-auth.json)
// The first two let a newer signature table be dropped in without a rebuild.
// Returns nullopt if none of them loads; Google Cast is then disabled.
const std::optional<cast::CastAuthCredentials>& castCredentials();

} // namespace airshow
