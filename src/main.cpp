#include <QGuiApplication>
#include <QDir>
#include <QQmlContext>
#include <QStandardPaths>
#include <QNetworkInterface>
#include <gst/gst.h>
#include "pipeline/MediaPipeline.h"
#include "ui/ReceiverWindow.h"
#include "ui/ConnectionBridge.h"
#include "settings/AppSettings.h"
#include "security/SecurityManager.h"
#include "discovery/DiscoveryManager.h"
#include "discovery/UpnpAdvertiser.h"
#include "platform/WindowsFirewall.h"
#include "protocol/AirPlayHandler.h"
#include "protocol/CastHandler.h"
#include "protocol/DlnaHandler.h"
#include "protocol/MiracastHandler.h"
#include "protocol/AirShowHandler.h"
#include "protocol/ProtocolManager.h"
#include "web/WebInterface.h"

static void checkRequiredPlugins() {
    struct { const char* name; const char* pkg; } required[] = {
        {"qml6glsink",    "gstreamer1.0-qt6"},
        {"videotestsrc",  "gstreamer1.0-plugins-base"},
        {"audiotestsrc",  "gstreamer1.0-plugins-base"},
        {"autoaudiosink", "gstreamer1.0-plugins-good"},
        {"avdec_h264",    "gstreamer1.0-libav"},
        {"h264parse",     "gstreamer1.0-plugins-bad"},
        {"avdec_aac",     "gstreamer1.0-libav"},
        // Cast-specific plugins (Phase 6)
        {"webrtcbin",     "gstreamer1.0-plugins-bad"},
        {"rtpvp8depay",   "gstreamer1.0-plugins-good"},
        {"rtpopusdepay",  "gstreamer1.0-plugins-good"},
        {"opusdec",       "gstreamer1.0-plugins-base"},
    };
    for (auto& p : required) {
        if (!gst_registry_check_feature_version(
                gst_registry_get(), p.name, 1, 20, 0)) {
            qFatal("Missing GStreamer plugin '%s'. Install package: %s",
                   p.name, p.pkg);
        }
    }

    // Non-fatal warning: vp8dec (Cast VP8 hardware/software decode, avdec_vp8 is the fallback)
    if (!gst_registry_check_feature_version(gst_registry_get(), "vp8dec", 1, 20, 0)) {
        qWarning("GStreamer plugin 'vp8dec' not found — Cast VP8 will use avdec_vp8 fallback (gst-libav)");
    }

    // Non-fatal warning: nicesrc (ICE for webrtcbin, Pitfall 4)
    if (!gst_registry_check_feature_version(gst_registry_get(), "nicesrc", 0, 1, 14)) {
        qWarning("GStreamer plugin 'nicesrc' not found — Cast WebRTC may fail. Install: gstreamer1.0-nice");
    }

    // Miracast-specific plugin checks (Phase 8)
    // Fatal: MPEG-TS demux pipeline — required for all Miracast streams
    struct { const char* name; const char* pkg; } miracastRequired[] = {
        {"rtpmp2tdepay", "gstreamer1.0-plugins-good"},
        {"tsparse",      "gstreamer1.0-plugins-bad"},
        {"tsdemux",      "gstreamer1.0-plugins-bad"},
    };
    for (auto& p : miracastRequired) {
        if (!gst_registry_check_feature_version(
                gst_registry_get(), p.name, 1, 20, 0)) {
            qFatal("Missing GStreamer plugin '%s' (required for Miracast). Install package: %s",
                   p.name, p.pkg);
        }
    }

    // Non-fatal: vaapidecodebin — hardware H.264 decode; avdec_h264 is the software fallback
    if (!gst_registry_check_feature_version(gst_registry_get(), "vaapidecodebin", 1, 20, 0)) {
        qWarning("GStreamer plugin 'vaapidecodebin' not found — Miracast will use avdec_h264 software decode");
    }

    // Non-fatal: aacparse — some Miracast streams carry LPCM only; AAC parsing is optional
    if (!gst_registry_check_feature_version(gst_registry_get(), "aacparse", 1, 20, 0)) {
        qWarning("GStreamer plugin 'aacparse' not found — Miracast AAC audio streams may not decode correctly");
    }
}

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    checkRequiredPlugins();

    // Set organization/app name so QSettings uses correct native path
    QCoreApplication::setOrganizationName("AirShow");
    QCoreApplication::setApplicationName("AirShow");

    QGuiApplication app(argc, argv);

    airshow::AppSettings settings;

    // Phase 7: SecurityManager — must outlive all protocol handlers.
    // Constructed after AppSettings (dependency injection) and before any handler.
    airshow::SecurityManager securityManager(settings);

    // Windows firewall rules — first launch only (D-12/D-13/D-14)
    if (settings.isFirstLaunch()) {
        if (!airshow::WindowsFirewall::registerRules()) {
            // D-13: permission denied — show actionable message
            qCritical("Firewall rules could not be registered automatically. "
                      "Please open the following ports manually:\n"
                      "  UDP 5353 (mDNS), UDP 1900 (SSDP),\n"
                      "  TCP 7000 (AirPlay), TCP 8009 (Google Cast),\n"
                      "  TCP 7400 (AirShow), TCP 7401 (Web Interface)");
        }
        settings.setFirstLaunchComplete();
    }

    airshow::DiscoveryManager discovery(&settings);
    if (!discovery.start()) {
        qWarning("Discovery failed to start — receiver will not appear in device pickers");
    }

    // DLNA SSDP advertisement (DISC-03) — construct only, start AFTER DlnaHandler wired
    const std::string dlnaXmlPath =
        QCoreApplication::applicationDirPath().toStdString()
        + "/resources/dlna/MediaRenderer.xml";
    airshow::UpnpAdvertiser upnpAdvertiser(&settings, dlnaXmlPath);

    airshow::MediaPipeline pipeline;
    airshow::ReceiverWindow window(pipeline, settings);

    // Phase 6: Pre-register pipeline for Cast WebRTC deferred pipeline creation.
    // ReceiverWindow::load() calls pipeline.setQmlVideoItem(videoItem) again with
    // the real QML VideoOutput pointer after sceneGraphInitialized fires.
    // This pre-call establishes that setQmlVideoItem is part of startup wiring.
    pipeline.setQmlVideoItem(nullptr);  // will be overridden by ReceiverWindow after QML loads

    if (!window.load()) {
        qCritical("Failed to start AirShow");
        return 1;
    }

    // Phase 7: Wire SecurityManager to ConnectionBridge (approval dialog state) and
    // expose it as a QML context property so QML can call resolveApproval().
    QObject::connect(&securityManager, &airshow::SecurityManager::requestApproval,
                     window.connectionBridge(),
                     &airshow::ConnectionBridge::showApprovalRequest);
    window.engine()->rootContext()->setContextProperty(
        QStringLiteral("securityManager"), &securityManager);

    // Protocol manager owns all protocol handlers
    airshow::ProtocolManager protocolManager(&pipeline);

    // DLNA handler (Phase 5) — must be created before upnpAdvertiser.start()
    // so the SOAP callback cookie points to a live handler.
    {
        auto dlnaHandler = std::make_unique<airshow::DlnaHandler>(window.connectionBridge());
        auto* dlnaRawPtr = dlnaHandler.get(); // raw ptr for UpnpAdvertiser before ownership transfer

        // Phase 7: Wire SecurityManager for SOAP action approval + RFC1918 filter
        dlnaRawPtr->setSecurityManager(&securityManager);

        // Route SOAP actions from UpnpAdvertiser to DlnaHandler (D-02)
        upnpAdvertiser.setDlnaHandler(dlnaRawPtr);

        protocolManager.addHandler(std::move(dlnaHandler));
    }

    // AirPlay handler (Phase 4)
    {
        std::string deviceId = discovery.deviceId();
        std::string keyfilePath = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                   + "/airplay.key").toStdString();
        // Ensure the directory exists
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

        auto airplay = std::make_unique<airshow::AirPlayHandler>(
            window.connectionBridge(),   // ConnectionBridge* for HUD updates
            &discovery,                  // DiscoveryManager* for pk TXT record update
            deviceId,
            keyfilePath);
        // Phase 7: Wire SecurityManager for connection approval + PIN pairing
        airplay->setSecurityManager(&securityManager);
        protocolManager.addHandler(std::move(airplay));
        // NOTE: addHandler() calls handler->setMediaPipeline(m_pipeline) internally.
        // This ensures AirPlayHandler::m_pipeline is non-null before start() is called.
        // Verified in ProtocolManager.cpp. Without this, all frame injection
        // via appsrc silently fails (AIRP-01, AIRP-02, AIRP-03 all break).
    }

    // Google Cast handler (Phase 6)
    {
        auto castHandler = std::make_unique<airshow::CastHandler>(
            window.connectionBridge());
        // Phase 7: Wire SecurityManager for async approval + RFC1918 filter
        castHandler->setSecurityManager(&securityManager);
        protocolManager.addHandler(std::move(castHandler));
        // NOTE: addHandler() calls handler->setMediaPipeline(m_pipeline) internally.
    }

    // Miracast (MS-MICE) handler (Phase 8) — Windows wireless display mirroring (MIRA-01)
    {
        auto miracastHandler = std::make_unique<airshow::MiracastHandler>(
            window.connectionBridge());
        // Phase 7: Wire SecurityManager for connection approval + RFC1918 filter
        miracastHandler->setSecurityManager(&securityManager);
        // QML video item pointer: nullptr here — the real pointer is stored in
        // MediaPipeline::m_qmlVideoItem (set by ReceiverWindow after sceneGraphInitialized).
        // MiracastHandler passes m_qmlVideoItem to initMiracastPipeline() at session time.
        // Same deferred-pointer pattern as pipeline.setQmlVideoItem(nullptr) above.
        miracastHandler->setQmlVideoItem(nullptr);
        protocolManager.addHandler(std::move(miracastHandler));
        // NOTE: addHandler() calls handler->setMediaPipeline(m_pipeline) internally.
    }

    // AirShow custom protocol handler (Phase 9) -- companion sender on port 7400
    {
        auto airshowHandler = std::make_unique<airshow::AirShowHandler>(
            window.connectionBridge());
        airshowHandler->setSecurityManager(&securityManager);
        protocolManager.addHandler(std::move(airshowHandler));
        // NOTE: addHandler() calls handler->setMediaPipeline(m_pipeline) internally.
    }

    // Start DLNA advertisement now that DlnaHandler is wired via setDlnaHandler (D-02)
    if (!upnpAdvertiser.start()) {
        qWarning("DLNA advertisement failed — renderer will not appear in DLNA controllers");
    }

    if (!protocolManager.startAll()) {
        qWarning("One or more protocol handlers failed to start");
    }

    // Web interface on port 7401 — serves landing page with QR code + sender app download
    airshow::WebInterface webInterface(&settings);
    // Check for a bundled sender APK in the app directory
    QString apkPath = QCoreApplication::applicationDirPath() + "/sender.apk";
    webInterface.setApkPath(apkPath);
    if (!webInterface.start()) {
        qWarning("Web interface failed to start on port 7401 — "
                 "landing page will not be available");
    } else {
        // Log the URLs for user convenience
        QStringList ips = QNetworkInterface::allInterfaces()
            .first().addressEntries().isEmpty()
            ? QStringList{} : QStringList{};
        for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
            if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
            if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
            for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol &&
                    !entry.ip().isLoopback()) {
                    qInfo("Web interface: http://%s:7401",
                          qPrintable(entry.ip().toString()));
                }
            }
        }
    }

    int result = app.exec();
    webInterface.stop();
    protocolManager.stopAll();
    discovery.stop();
    upnpAdvertiser.stop();
    return result;
}
