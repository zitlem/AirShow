import QtQuick
import QtQuick.Window
import QtQuick.Controls
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0

Window {
    id: root
    // D-10: Application launches fullscreen by default on the primary display
    visibility: Window.FullScreen
    color: "black"
    title: "MyAirShow"
    visible: true

    // GstGLQt6VideoItem hosts the qml6glsink render output.
    // objectName must match ReceiverWindow.cpp findChild("videoItem") call.
    // forceAspectRatio: true — D-01/DISP-01: letterbox/pillarbox, never stretch.
    // NOTE: fillMode does not exist on GstGLQt6VideoItem (RESEARCH.md Pitfall 1).
    GstGLQt6VideoItem {
        id: videoItem
        objectName: "videoItem"
        anchors.fill: parent
        forceAspectRatio: true   // D-01: letterbox — preserve aspect ratio, never stretch (default, explicit for clarity)
    }

    // DISP-03: Idle/waiting screen — visible when no device is connected.
    // Renders on top of the video item (black video when no stream = seamless).
    IdleScreen {
        anchors.fill: parent
    }

    // DISP-02: Connection status HUD — fades in on connect/disconnect, auto-hides after 3s.
    // Renders on top of the idle screen (appears during transition from idle to active).
    HudOverlay {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
    }

    // Mute toggle button — D-08 / FOUND-04
    // Styled to match overlay aesthetic (D-15): dark semi-transparent background, white text.
    // Wires to AudioBridge QObject exposed as audioBridge context property.
    Item {
        id: muteButton
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 24
        }
        width: muteLabel.width + 32
        height: muteLabel.height + 16
        opacity: 0.8

        Rectangle {
            anchors.fill: parent
            color: "#B3000000"
            radius: 6
        }

        Text {
            id: muteLabel
            anchors.centerIn: parent
            text: audioBridge.muted ? "Unmute" : "Mute"
            color: "white"
            font.pixelSize: 16
            font.family: "sans-serif"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: audioBridge.setMuted(!audioBridge.muted)
        }
    }
}
