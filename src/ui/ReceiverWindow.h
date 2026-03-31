#pragma once
#include <QQmlApplicationEngine>
#include <memory>
#include "settings/AppSettings.h"

namespace airshow {
class MediaPipeline;
class ConnectionBridge;

class ReceiverWindow {
public:
    explicit ReceiverWindow(MediaPipeline& pipeline, AppSettings& settings);
    ~ReceiverWindow() = default;

    // Load QML, retrieve GstGLVideoItem, wire pipeline
    bool load();

    // Public accessor for ConnectionBridge — needed by AirPlayHandler for HUD updates.
    // Returns nullptr if load() has not been called yet.
    ConnectionBridge* connectionBridge() { return m_connectionBridge; }

    // Public accessor for the QML engine — used to expose C++ objects as context properties.
    QQmlApplicationEngine* engine() { return &m_engine; }

private:
    QQmlApplicationEngine m_engine;
    MediaPipeline&        m_pipeline;
    AppSettings&          m_settings;
    ConnectionBridge*     m_connectionBridge    = nullptr;
    bool                  m_pipelineInitialized = false;
};

} // namespace airshow
