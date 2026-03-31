import QtQuick

Item {
    id: idleScreen
    anchors.fill: parent

    // D-09: visible when not connected; hides when session starts; reappears on end
    visible: !connectionBridge.connected

    // Dark background fill — D-08 (Window already black; this ensures full coverage)
    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Column {
        anchors.centerIn: parent
        spacing: 16

        // App name — D-08
        Text {
            text: "AirShow"
            color: "white"
            font.pixelSize: 48
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Receiver name live-bound to appSettings — D-10
        Text {
            text: appSettings.receiverName
            color: "#CCCCCC"
            font.pixelSize: 28
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Pulsing "Waiting..." text — D-11 (subtle opacity animation, TV-receiver-like)
        Text {
            id: waitingText
            text: "Waiting for connection..."
            color: "white"
            font.pixelSize: 22
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter

            SequentialAnimation on opacity {
                // CRITICAL: running only when idle screen is visible (RESEARCH.md Pitfall 4)
                running: idleScreen.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 1200; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
            }
        }

        // PIN display — SEC-02 / D-05 / D-14
        // Visible only when pinEnabled is true and pin is non-empty.
        // Renders below the waiting text so new devices can enter the PIN.
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: pinColumn.width
            height: pinColumn.height
            visible: appSettings.pinEnabled && appSettings.pin.length > 0

            Column {
                id: pinColumn
                spacing: 4
                anchors.horizontalCenter: parent.horizontalCenter

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "PIN"
                    color: "white"
                    font.pixelSize: 14
                    font.family: "sans-serif"
                    opacity: 0.7
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: appSettings.pin
                    color: "white"
                    font.pixelSize: 48
                    font.bold: true
                    font.family: "sans-serif"
                    font.letterSpacing: 8
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
