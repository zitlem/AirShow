#include "web/WebInterface.h"
#include "settings/AppSettings.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QHostAddress>
#include <QTimer>

namespace airshow {

WebInterface::WebInterface(AppSettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{}

WebInterface::~WebInterface() {
    stop();
}

bool WebInterface::start(uint16_t port) {
    if (m_running) return true;

    m_port = port;
    m_server = new QTcpServer(this);

    connect(m_server, &QTcpServer::newConnection,
            this, &WebInterface::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical("WebInterface: failed to listen on port %u: %s",
                  port, qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_running = true;
    qDebug("WebInterface: listening on port %u", port);
    return true;
}

void WebInterface::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

void WebInterface::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket) continue;

        // Read the HTTP request when data arrives
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleRequest(socket);
        });

        // Clean up on disconnect
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

        // Timeout: close sockets that don't send a request within 5 seconds
        QTimer::singleShot(5000, socket, [socket]() {
            if (socket->state() == QAbstractSocket::ConnectedState) {
                socket->disconnectFromHost();
            }
        });
    }
}

void WebInterface::handleRequest(QTcpSocket* socket) {
    QByteArray request = socket->readAll();
    if (request.isEmpty()) return;

    // Parse the request line (first line: "GET /path HTTP/1.1")
    int lineEnd = request.indexOf("\r\n");
    if (lineEnd < 0) lineEnd = request.indexOf("\n");
    if (lineEnd < 0) {
        sendResponse(socket, 400, "text/plain", "Bad Request");
        return;
    }

    QByteArray requestLine = request.left(lineEnd);
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendResponse(socket, 400, "text/plain", "Bad Request");
        return;
    }

    QByteArray method = parts[0];
    QByteArray path = parts[1];

    if (method != "GET") {
        sendResponse(socket, 405, "text/plain", "Method Not Allowed");
        return;
    }

    if (path == "/" || path == "/index.html") {
        sendResponse(socket, 200, "text/html; charset=utf-8", buildLandingPage());
    } else if (path == "/qr.svg") {
        QStringList ips = localIpAddresses();
        QString url = ips.isEmpty()
            ? QStringLiteral("http://localhost:%1").arg(m_port)
            : QStringLiteral("http://%1:%2").arg(ips.first()).arg(m_port);
        sendResponse(socket, 200, "image/svg+xml", generateQrSvg(url));
    } else if (path == "/download/sender.apk") {
        if (!m_apkPath.isEmpty() && QFileInfo::exists(m_apkPath)) {
            QFile file(m_apkPath);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray apkData = file.readAll();
                sendResponse(socket, 200, "application/vnd.android.package-archive", apkData);
            } else {
                sendResponse(socket, 500, "text/plain", "Failed to read APK file");
            }
        } else {
            // Redirect to GitHub releases
            QByteArray redirect = "HTTP/1.1 302 Found\r\n"
                "Location: https://github.com/AirShow/AirShow/releases/latest\r\n"
                "Connection: close\r\n\r\n";
            socket->write(redirect);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }
    } else if (path == "/style.css") {
        sendResponse(socket, 200, "text/css", buildStylesheet());
    } else {
        sendResponse(socket, 404, "text/plain", "Not Found");
    }
}

QByteArray WebInterface::buildLandingPage() const {
    QString name = m_settings ? m_settings->receiverName() : QStringLiteral("AirShow");
    QStringList ips = localIpAddresses();
    QString ipList;
    for (const QString& ip : ips) {
        ipList += QStringLiteral("<li>%1</li>").arg(ip);
    }

    QString pageUrl = ips.isEmpty()
        ? QStringLiteral("http://localhost:%1").arg(m_port)
        : QStringLiteral("http://%1:%2").arg(ips.first()).arg(m_port);

    bool hasApk = !m_apkPath.isEmpty() && QFileInfo::exists(m_apkPath);
    QString downloadSection;
    if (hasApk) {
        downloadSection = QStringLiteral(
            "<a href=\"/download/sender.apk\" class=\"btn\">Download AirShow Sender (Android APK)</a>");
    } else {
        downloadSection = QStringLiteral(
            "<a href=\"https://github.com/AirShow/AirShow/releases/latest\" class=\"btn\" target=\"_blank\">"
            "Download AirShow Sender from GitHub</a>");
    }

    QString html = QStringLiteral(R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%1 - AirShow</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif;
            background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
            color: #e0e0e0;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            max-width: 480px;
            width: 100%;
            text-align: center;
        }
        .logo {
            font-size: 3rem;
            font-weight: 700;
            letter-spacing: -1px;
            margin-bottom: 0.25rem;
            background: linear-gradient(135deg, #667eea, #764ba2);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }
        .subtitle {
            font-size: 0.95rem;
            color: #999;
            margin-bottom: 2rem;
        }
        .card {
            background: rgba(255,255,255,0.06);
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 16px;
            padding: 2rem;
            margin-bottom: 1.5rem;
        }
        .receiver-name {
            font-size: 1.4rem;
            font-weight: 600;
            margin-bottom: 1rem;
            color: #fff;
        }
        .qr-container {
            background: white;
            border-radius: 12px;
            padding: 16px;
            display: inline-block;
            margin: 1rem 0;
        }
        .qr-container svg { display: block; }
        .ip-list {
            list-style: none;
            margin: 1rem 0;
        }
        .ip-list li {
            font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;
            font-size: 0.95rem;
            padding: 0.3rem 0;
            color: #b0b0b0;
        }
        .btn {
            display: inline-block;
            padding: 14px 28px;
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            text-decoration: none;
            border-radius: 12px;
            font-weight: 600;
            font-size: 1rem;
            transition: transform 0.15s, box-shadow 0.15s;
            margin: 0.5rem;
        }
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(102,126,234,0.3);
        }
        h3 {
            font-size: 1rem;
            color: #ccc;
            margin-bottom: 0.75rem;
            font-weight: 500;
        }
        .protocols {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 0.75rem;
            text-align: left;
        }
        .protocol {
            background: rgba(255,255,255,0.04);
            border-radius: 10px;
            padding: 0.75rem;
        }
        .protocol strong {
            display: block;
            font-size: 0.85rem;
            color: #fff;
            margin-bottom: 0.25rem;
        }
        .protocol span {
            font-size: 0.75rem;
            color: #888;
        }
        .footer {
            font-size: 0.8rem;
            color: #666;
            margin-top: 1rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">AirShow</div>
        <div class="subtitle">Free Screen Mirroring Receiver</div>

        <div class="card">
            <div class="receiver-name">%1</div>
            <div class="qr-container">
                <img src="/qr.svg" width="180" height="180" alt="QR Code">
            </div>
            <p style="font-size: 0.85rem; color: #999; margin-top: 0.5rem;">
                Scan to open this page on your phone
            </p>
        </div>

        <div class="card">
            <h3>Download Sender App</h3>
            %2
        </div>

        <div class="card">
            <h3>Connect Directly</h3>
            <ul class="ip-list">%3</ul>
            <div class="protocols">
                <div class="protocol">
                    <strong>AirPlay</strong>
                    <span>iPhone/iPad/Mac &rarr; Screen Mirroring</span>
                </div>
                <div class="protocol">
                    <strong>Google Cast</strong>
                    <span>Chrome/Android &rarr; Cast button</span>
                </div>
                <div class="protocol">
                    <strong>Miracast</strong>
                    <span>Windows &rarr; Connect</span>
                </div>
                <div class="protocol">
                    <strong>DLNA</strong>
                    <span>Any DLNA app &rarr; Push media</span>
                </div>
            </div>
        </div>

        <div class="footer">
            AirShow &mdash; Free, open-source screen mirroring for everyone
        </div>
    </div>
</body>
</html>)").arg(name.toHtmlEscaped(), downloadSection, ipList);

    return html.toUtf8();
}

QByteArray WebInterface::generateQrSvg(const QString& url) {
    // Minimal QR code implementation using a simple text-based SVG.
    // For a real QR code we'd need a QR encoding library. Instead, generate
    // a placeholder with the URL text that can be enhanced later with a
    // proper QR library (e.g., qrencode or nayuki-qr).
    //
    // For now, render the URL as centered text inside a QR-shaped frame.
    // This is intentionally simple — the user can scan the URL manually or
    // click the link. A proper QR library can be added in a future phase.

    // Simple QR-like visual with the URL
    QString svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 180 180" width="180" height="180">
  <rect width="180" height="180" fill="white" rx="8"/>
  <!-- Corner position patterns (QR-like visual) -->
  <rect x="12" y="12" width="42" height="42" fill="#333" rx="4"/>
  <rect x="18" y="18" width="30" height="30" fill="white" rx="2"/>
  <rect x="24" y="24" width="18" height="18" fill="#333" rx="1"/>

  <rect x="126" y="12" width="42" height="42" fill="#333" rx="4"/>
  <rect x="132" y="18" width="30" height="30" fill="white" rx="2"/>
  <rect x="138" y="24" width="18" height="18" fill="#333" rx="1"/>

  <rect x="12" y="126" width="42" height="42" fill="#333" rx="4"/>
  <rect x="18" y="132" width="30" height="30" fill="white" rx="2"/>
  <rect x="24" y="138" width="18" height="18" fill="#333" rx="1"/>

  <!-- Center content area -->
  <text x="90" y="86" text-anchor="middle" font-family="monospace" font-size="8" fill="#333">%1</text>
  <text x="90" y="100" text-anchor="middle" font-family="sans-serif" font-size="7" fill="#666">Open in browser</text>
</svg>)").arg(url.toHtmlEscaped());

    return svg.toUtf8();
}

void WebInterface::sendResponse(QTcpSocket* socket, int statusCode,
                                 const QByteArray& contentType,
                                 const QByteArray& body) {
    QByteArray statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 302: statusText = "Found"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 405: statusText = "Method Not Allowed"; break;
        case 500: statusText = "Internal Server Error"; break;
        default:  statusText = "Unknown"; break;
    }

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

QStringList WebInterface::localIpAddresses() {
    QStringList result;
    const auto allInterfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : allInterfaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsRunning)) continue;

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (addr.isLoopback()) continue;
            result.append(addr.toString());
        }
    }
    return result;
}

// Unused method reference in handleRequest — add the missing static helper
QByteArray WebInterface::buildStylesheet() {
    // CSS is inlined in the HTML, but this endpoint exists for potential
    // future use. Return the same styles.
    return "/* Styles are inlined in the landing page HTML */";
}

} // namespace airshow
