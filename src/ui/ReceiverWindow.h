#pragma once
#include <QQmlApplicationEngine>
#include <memory>

namespace myairshow {
class MediaPipeline;

class ReceiverWindow {
public:
    explicit ReceiverWindow(MediaPipeline& pipeline);
    ~ReceiverWindow() = default;

    // Load QML, retrieve GstGLVideoItem, wire pipeline
    bool load();

private:
    QQmlApplicationEngine m_engine;
    MediaPipeline&        m_pipeline;
};

} // namespace myairshow
