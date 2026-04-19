import QtQuick
import QtQuick.Window
import QtQuick.Controls
import AirShow 1.0

Window {
    id: root
    // D-10: Application launches fullscreen by default on the primary display.
    // Pass --windowed on the command line to run in a normal window instead.
    visibility: Qt.application.arguments.indexOf("--windowed") !== -1
                ? Window.Windowed : Window.FullScreen
    width: 1280
    height: 720
    color: "black"
    title: "AirShow"
    visible: true

    // VideoFrameSink receives RGBA frames from GStreamer's appsink and renders
    // them via Qt's scene graph (QSGSimpleTextureNode). Replaces GstGLQt6VideoItem
    // to avoid GStreamer GL context sharing and buffer pool deadlock issues.
    // objectName must match ReceiverWindow.cpp findChild("videoItem") call.
    VideoFrameSink {
        id: videoItem
        objectName: "videoItem"
        anchors.fill: parent
        forceAspectRatio: true   // D-01: letterbox — preserve aspect ratio, never stretch
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

    // SEC-01: Approval dialog overlay — appears on top of everything when a new device
    // requests to connect and requireApproval is enabled. Must be the last child so
    // it renders above all other overlays (HudOverlay, mute button, close button).
    ApprovalDialog {
        anchors.fill: parent
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

    // Top-right button row — fullscreen toggle + close button
    Row {
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 16
            rightMargin: 16
        }
        spacing: 8
        opacity: mouseTracker.controlsVisible ? 0.9 : 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 200 } }

        // Fullscreen toggle button
        Item {
            id: fullscreenButton
            width: 40
            height: 40

            Rectangle {
                anchors.fill: parent
                color: fsMouseArea.containsMouse ? "#E0AAAAFF" : "#B3000000"
                radius: 20
            }

            Text {
                anchors.centerIn: parent
                text: "\u26F6"
                color: "white"
                font.pixelSize: 20
                font.family: "sans-serif"
            }

            MouseArea {
                id: fsMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.visibility = root.visibility === Window.FullScreen
                        ? Window.Windowed
                        : Window.FullScreen
                }
            }
        }

        // Close button
        Item {
            id: closeButton
            width: 40
            height: 40

            Rectangle {
                anchors.fill: parent
                color: closeMouseArea.containsMouse ? "#E0FF4444" : "#B3000000"
                radius: 20
            }

            Text {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 2
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
