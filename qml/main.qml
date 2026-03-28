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
    GstGLQt6VideoItem {
        id: videoItem
        objectName: "videoItem"
        anchors.fill: parent
    }

    // Mute toggle button — D-08 / FOUND-04
    // Wires to AudioBridge QObject exposed as audioBridge context property.
    Button {
        id: muteButton
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 24
        }
        text: audioBridge.muted ? "Unmute" : "Mute"
        onClicked: audioBridge.setMuted(!audioBridge.muted)
        opacity: 0.8
    }
}
