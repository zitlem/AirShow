#pragma once
#include <string>

namespace airshow {

enum class DecoderType {
    Hardware,
    Software
};

struct DecoderInfo {
    std::string elementName;   // e.g. "vaapih264dec", "avdec_h264"
    DecoderType type;
};

} // namespace airshow
