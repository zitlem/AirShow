#include "ui/AudioBridge.h"
#include "pipeline/MediaPipeline.h"

namespace airshow {

AudioBridge::AudioBridge(MediaPipeline& pipeline, QObject* parent)
    : QObject(parent), m_pipeline(pipeline) {}

bool AudioBridge::isMuted() const {
    return m_pipeline.isMuted();
}

void AudioBridge::setMuted(bool muted) {
    if (m_pipeline.isMuted() == muted) return;
    m_pipeline.setMuted(muted);
    emit mutedChanged(muted);
}

} // namespace airshow
