#pragma once
#include <QQmlApplicationEngine>
#include <memory>
#include "settings/AppSettings.h"

namespace myairshow {
class MediaPipeline;

class ReceiverWindow {
public:
    explicit ReceiverWindow(MediaPipeline& pipeline, AppSettings& settings);
    ~ReceiverWindow() = default;

    // Load QML, retrieve GstGLVideoItem, wire pipeline
    bool load();

private:
    QQmlApplicationEngine m_engine;
    MediaPipeline&        m_pipeline;
    AppSettings&          m_settings;
};

} // namespace myairshow
