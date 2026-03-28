#include <QGuiApplication>
#include <gst/gst.h>
#include "pipeline/MediaPipeline.h"
#include "ui/ReceiverWindow.h"
#include "settings/AppSettings.h"
#include "discovery/DiscoveryManager.h"
#include "discovery/UpnpAdvertiser.h"
#include "platform/WindowsFirewall.h"

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

    // Set organization/app name so QSettings uses correct native path
    QCoreApplication::setOrganizationName("MyAirShow");
    QCoreApplication::setApplicationName("MyAirShow");

    QGuiApplication app(argc, argv);

    myairshow::AppSettings settings;

    // Register firewall rules on Windows first launch (Plan 03 implements WindowsFirewall)
    // Placeholder comment — wired in Plan 03

    myairshow::DiscoveryManager discovery(&settings);
    if (!discovery.start()) {
        qWarning("Discovery failed to start — receiver will not appear in device pickers");
    }

    myairshow::MediaPipeline pipeline;
    myairshow::ReceiverWindow window(pipeline);

    if (!window.load()) {
        qCritical("Failed to start MyAirShow");
        return 1;
    }

    return app.exec();
}
