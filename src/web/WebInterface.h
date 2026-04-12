#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <cstdint>

namespace airshow {

class AppSettings;

// Lightweight HTTP server on port 7401 serving the AirShow landing page.
//
// The landing page shows:
//   - Receiver name and connection status
//   - QR code linking to this page (for easy phone access)
//   - Download link for the AirShow sender APK (served from embedded resource)
//   - Instructions for connecting via AirPlay, Cast, DLNA
//
// This is a minimal HTTP/1.1 server — no framework, no TLS, no WebSocket.
// Only serves GET requests for a small set of static paths.
class WebInterface : public QObject {
    Q_OBJECT
public:
    explicit WebInterface(AppSettings* settings, QObject* parent = nullptr);
    ~WebInterface() override;

    // Start listening on port 7401. Returns false if bind fails.
    bool start(uint16_t port = 7401);

    // Stop the server.
    void stop();

    bool isRunning() const { return m_running; }

    // Set the path to the sender APK file to serve for download.
    // If empty or file doesn't exist, the download link will point to GitHub releases.
    void setApkPath(const QString& path) { m_apkPath = path; }

private slots:
    void onNewConnection();

private:
    // Handle a single HTTP request from a connected socket.
    void handleRequest(QTcpSocket* socket);

    // Generate the landing page HTML.
    QByteArray buildLandingPage() const;

    // Generate a simple SVG QR code for the given URL.
    static QByteArray generateQrSvg(const QString& url);

    // Build the CSS stylesheet.
    static QByteArray buildStylesheet();

    // Send an HTTP response.
    static void sendResponse(QTcpSocket* socket, int statusCode,
                              const QByteArray& contentType,
                              const QByteArray& body);

    // Get this machine's local IP addresses for display.
    static QStringList localIpAddresses();

    QTcpServer* m_server   = nullptr;
    AppSettings* m_settings = nullptr;
    uint16_t    m_port     = 7401;
    bool        m_running  = false;
    QString     m_apkPath;
};

} // namespace airshow
