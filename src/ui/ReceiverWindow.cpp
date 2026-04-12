#include "ui/ReceiverWindow.h"
#include "pipeline/MediaPipeline.h"
#include "ui/AudioBridge.h"
#include "ui/ConnectionBridge.h"
#include "ui/SettingsBridge.h"
#include "ui/VideoFrameSink.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QQuickWindow>
#include <QDebug>

namespace airshow {

ReceiverWindow::ReceiverWindow(MediaPipeline& pipeline, AppSettings& settings)
    : m_pipeline(pipeline), m_settings(settings) {}

bool ReceiverWindow::load() {
    // VideoFrameSink is a C++ QML_ELEMENT registered in the AirShow module.
    // No GStreamer preloading needed (we use appsink, not qml6glsink).

    // Expose AudioBridge to QML as "audioBridge" context property.
    // AudioBridge is parented to the QML engine so it is destroyed with it.
    auto* audioBridge = new AudioBridge(m_pipeline, &m_engine);
    m_engine.rootContext()->setContextProperty("audioBridge", audioBridge);

    // Per D-05: ConnectionBridge exposes connected/deviceName/protocol to QML.
    // Phase 4 protocol handlers will call connBridge->setConnected() to drive UI state.
    auto* connBridge = new ConnectionBridge(&m_engine);
    m_connectionBridge = connBridge;  // Store for connectionBridge() accessor
    m_engine.rootContext()->setContextProperty("connectionBridge", connBridge);

    // Per D-10: SettingsBridge exposes receiverName to QML for the idle screen.
    // Phase 7 will wire live updates; Phase 3 reads the name once at startup.
    auto* settingsBridge = new SettingsBridge(m_settings, &m_engine);
    m_engine.rootContext()->setContextProperty("appSettings", settingsBridge);

    // Load QML (GstGLQt6VideoItem is now registered, all context properties set)
    m_engine.load(QUrl(QStringLiteral("qrc:/AirShow/qml/main.qml")));

    if (m_engine.rootObjects().isEmpty()) {
        qWarning("ReceiverWindow::load — QML engine failed to load main.qml");
        return false;
    }

    // qml6glsink needs the QQuickWindow's OpenGL context, which doesn't exist
    // until the scene graph is initialized. Connect to that signal and defer
    // pipeline init until the GL context is ready.
    QObject* rootObject = m_engine.rootObjects().first();
    auto* window = qobject_cast<QQuickWindow*>(rootObject);
    if (window) {
        QObject::connect(window, &QQuickWindow::sceneGraphInitialized,
                         [this, rootObject]() {
            if (m_pipelineInitialized) return;
            m_pipelineInitialized = true;

            auto* videoItem = rootObject->findChild<VideoFrameSink*>("videoItem");
            if (!videoItem) {
                qWarning("ReceiverWindow — could not find VideoFrameSink named 'videoItem'");
                return;
            }

            // Phase 6: store the QML video item pointer for deferred Cast WebRTC pipeline.
            m_pipeline.setQmlVideoItem(videoItem);

            // Wire decoded video frames from GStreamer appsink → VideoFrameSink.
            // The callback runs on GStreamer's streaming thread; VideoFrameSink::pushFrame()
            // is thread-safe (mutex + QueuedConnection invokeMethod).
            m_pipeline.setVideoFrameCallback([videoItem](QImage frame) {
                videoItem->pushFrame(frame);
            });

            // Phase 4: build the appsrc pipeline. No GL context dependency — appsink
            // delivers RGBA frames to VideoFrameSink via CPU memory, no GL involved.
            if (!m_pipeline.initAppsrcPipeline(videoItem)) {
                qWarning("ReceiverWindow — MediaPipeline::initAppsrcPipeline() failed");
                return;
            }
        });
    } else {
        qWarning("ReceiverWindow::load — root object is not a QQuickWindow");
        return false;
    }

    return true;
}

} // namespace airshow
