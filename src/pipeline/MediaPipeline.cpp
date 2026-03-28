#include "pipeline/MediaPipeline.h"

namespace myairshow {

MediaPipeline::MediaPipeline()  = default;
MediaPipeline::~MediaPipeline() { stop(); }

bool MediaPipeline::init(void* /*qmlVideoItem*/) { return false; }
void MediaPipeline::setMuted(bool /*muted*/) {}
bool MediaPipeline::isMuted() const { return m_muted; }
std::optional<DecoderInfo> MediaPipeline::activeDecoder() const { return m_activeDecoder; }
void MediaPipeline::setDecoderSelectedCallback(DecoderSelectedCallback cb) { m_decoderCallback = std::move(cb); }
void MediaPipeline::stop() {}

} // namespace myairshow
