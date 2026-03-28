#include <QGuiApplication>
#include <gst/gst.h>
#include "pipeline/MediaPipeline.h"
#include "ui/ReceiverWindow.h"

static void checkRequiredPlugins() {
    struct { const char* name; const char* pkg; } required[] = {
        {"qml6glsink",    "gstreamer1.0-qt6"},
        {"videotestsrc",  "gstreamer1.0-plugins-base"},
        {"audiotestsrc",  "gstreamer1.0-plugins-base"},
        {"autoaudiosink", "gstreamer1.0-plugins-good"},
        {"avdec_h264",    "gstreamer1.0-libav"},
    };
    for (auto& p : required) {
        if (!gst_registry_check_feature_version(
                gst_registry_get(), p.name, 1, 20, 0)) {
            qFatal("Missing GStreamer plugin '%s'. Install package: %s",
                   p.name, p.pkg);
        }
    }
}

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    checkRequiredPlugins();

    QGuiApplication app(argc, argv);

    myairshow::MediaPipeline pipeline;
    myairshow::ReceiverWindow window(pipeline);

    if (!window.load()) {
        qWarning("ReceiverWindow::load() not yet implemented (stub)");
    }

    return app.exec();
}
