#pragma once
#include <QObject>

namespace myairshow {
class MediaPipeline;

// QObject bridge that exposes mute state to QML via Q_PROPERTY.
// QML binds to audioBridge.muted; the mute button calls audioBridge.setMuted(true/false).
class AudioBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)

public:
    explicit AudioBridge(MediaPipeline& pipeline, QObject* parent = nullptr);

    bool isMuted() const;
    void setMuted(bool muted);

signals:
    void mutedChanged(bool muted);

private:
    MediaPipeline& m_pipeline;
};

} // namespace myairshow
