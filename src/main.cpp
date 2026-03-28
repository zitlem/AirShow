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

    // Windows firewall rules — first launch only (D-12/D-13/D-14)
    if (settings.isFirstLaunch()) {
        if (!myairshow::WindowsFirewall::registerRules()) {
            // D-13: permission denied — show actionable message
            qCritical("Firewall rules could not be registered automatically. "
                      "Please open the following ports manually:\n"
                      "  UDP 5353 (mDNS), UDP 1900 (SSDP),\n"
                      "  TCP 7000 (AirPlay), TCP 8009 (Google Cast)");
        }
        settings.setFirstLaunchComplete();
    }

    myairshow::DiscoveryManager discovery(&settings);
    if (!discovery.start()) {
        qWarning("Discovery failed to start — receiver will not appear in device pickers");
    }

    // DLNA SSDP advertisement (DISC-03)
    const std::string dlnaXmlPath =
        QCoreApplication::applicationDirPath().toStdString()
        + "/resources/dlna/MediaRenderer.xml";
    myairshow::UpnpAdvertiser upnpAdvertiser(&settings, dlnaXmlPath);
    if (!upnpAdvertiser.start()) {
        qWarning("DLNA advertisement failed — renderer will not appear in DLNA controllers");
    }

    myairshow::MediaPipeline pipeline;
    myairshow::ReceiverWindow window(pipeline);

    if (!window.load()) {
        qCritical("Failed to start MyAirShow");
        return 1;
    }

    return app.exec();
}
