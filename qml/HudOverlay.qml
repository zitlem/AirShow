import QtQuick
import QtQuick.Controls

Item {
    id: hudOverlay

    // CRITICAL: use opacity > 0, not connectionBridge.connected
    // Prevents mouse-event blocking when faded out (RESEARCH.md Pitfall 3)
    visible: opacity > 0
    opacity: 0

    anchors {
        top: parent.top
        horizontalCenter: parent.horizontalCenter
        topMargin: 20
    }
    width: hudContent.width + 24
    height: hudContent.height + 16

    // Semi-transparent dark background — D-12 (~70% opacity black)
    Rectangle {
        anchors.fill: parent
        color: "#B3000000"
        radius: 8
    }

    // Protocol emoji + device name + " via " + protocol — D-04, D-07
    Text {
        id: hudContent
        anchors.centerIn: parent
        text: {
            var icon = ""
            if (connectionBridge.protocol === "AirPlay")   icon = "\uD83D\uDCF1 "   // 📱
            else if (connectionBridge.protocol === "Cast") icon = "\uD83D\uDCFA "   // 📺
            else if (connectionBridge.protocol === "Miracast") icon = "\uD83D\uDCBB " // 💻
            else if (connectionBridge.protocol === "DLNA") icon = "\uD83C\uDFB5 "   // 🎵
            return icon + connectionBridge.deviceName + " via " + connectionBridge.protocol
        }
        color: "white"
        font.pixelSize: 22
        font.family: "sans-serif"
    }

    // Fade in — 250ms, InOutQuad — D-14
    NumberAnimation {
        id: fadeIn
        target: hudOverlay
        property: "opacity"
        to: 1.0
        duration: 250
        easing.type: Easing.InOutQuad
    }

    // Fade out — 250ms, InOutQuad — D-14
    NumberAnimation {
        id: fadeOut
        target: hudOverlay
        property: "opacity"
        to: 0.0
        duration: 250
        easing.type: Easing.InOutQuad
    }

    // Auto-hide after 3 seconds of no state change — D-05
    Timer {
        id: hudHideTimer
        interval: 3000
        repeat: false
        onTriggered: fadeOut.start()
    }

    // React to connection state changes (both connect and disconnect show the HUD)
    Connections {
        target: connectionBridge
        function onConnectedChanged(c) {
            fadeOut.stop()
            hudOverlay.opacity = 0
            fadeIn.start()
            hudHideTimer.restart()
        }
    }
}
