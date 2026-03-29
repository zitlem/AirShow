#include "ui/ReceiverWindow.h"
#include "pipeline/MediaPipeline.h"
#include "ui/AudioBridge.h"
#include "ui/ConnectionBridge.h"
#include "ui/SettingsBridge.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QQuickWindow>
#include <gst/gst.h>
#include <QDebug>

namespace myairshow {

ReceiverWindow::ReceiverWindow(MediaPipeline& pipeline, AppSettings& settings)
    : m_pipeline(pipeline), m_settings(settings) {}

bool ReceiverWindow::load() {
    // CRITICAL (Anti-Pattern from RESEARCH.md Pitfall 4):
    // Preload qml6glsink BEFORE engine.load() to register GstGLVideoItem QML type.
    GstElement* preload = gst_element_factory_make("qml6glsink", nullptr);
    if (!preload) {
        qFatal("qml6glsink not available — install gstreamer1.0-qt6");
        return false;
    }
    gst_object_unref(preload);  // Side effect (type registration) already done.

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
    m_engine.load(QUrl(QStringLiteral("qrc:/MyAirShow/qml/main.qml")));

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

            QObject* videoItem = rootObject->findChild<QObject*>("videoItem");
            if (!videoItem) {
                qWarning("ReceiverWindow — could not find QML object named 'videoItem'");
            }

            // Phase 6: store the QML video item pointer for deferred Cast WebRTC pipeline
            // creation. CastSession::onWebrtc() calls initWebrtcPipeline() (no arg) which
            // uses this stored pointer. Must be called before any Cast connection arrives.
            m_pipeline.setQmlVideoItem(videoItem);

            if (!m_pipeline.init(videoItem)) {
                qWarning("ReceiverWindow — MediaPipeline::init() failed");
                return;
            }
            m_pipeline.play();
        });
    } else {
        qWarning("ReceiverWindow::load — root object is not a QQuickWindow");
        return false;
    }

    return true;
}

} // namespace myairshow
