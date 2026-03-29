import QtQuick
import QtQuick.Window
import QtQuick.Controls
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0

Window {
    id: root
    // D-10: Application launches fullscreen by default on the primary display.
    // Pass --windowed on the command line to run in a normal window instead.
    visibility: Qt.application.arguments.indexOf("--windowed") !== -1
                ? Window.Windowed : Window.FullScreen
    width: 1280
    height: 720
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

    // Track mouse movement to show/hide controls overlay
    MouseArea {
        id: mouseTracker
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true

        property bool controlsVisible: false

        onPositionChanged: {
            controlsVisible = true
            hideTimer.restart()
        }

        Timer {
            id: hideTimer
            interval: 3000
            onTriggered: mouseTracker.controlsVisible = false
        }
    }

    // Close button — appears top-right when mouse moves
    Item {
        id: closeButton
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 16
            rightMargin: 16
        }
        width: 40
        height: 40
        opacity: mouseTracker.controlsVisible ? 0.9 : 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 200 } }

        Rectangle {
            anchors.fill: parent
            color: hovered ? "#E0FF4444" : "#B3000000"
            radius: 20

            property bool hovered: closeMouseArea.containsMouse
        }

        Text {
            anchors.centerIn: parent
            text: "\u2715"
            color: "white"
            font.pixelSize: 20
            font.family: "sans-serif"
        }

        MouseArea {
            id: closeMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.quit()
        }
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
        opacity: mouseTracker.controlsVisible ? 0.8 : 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 200 } }

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
