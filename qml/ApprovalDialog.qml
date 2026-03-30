import QtQuick

// ApprovalDialog — modal approval overlay for incoming connection requests.
//
// D-01 / SEC-01: Appears when connectionBridge.approvalPending is true.
// D-13: Full-screen MouseArea blocks input to underlying content when visible.
// Follows the established HudOverlay pattern:
//   - visible: opacity > 0 (prevents mouse-event blocking at opacity 0, RESEARCH.md Pitfall 3)
//   - Behavior on opacity { NumberAnimation { duration: 150 } }
//   - Dark semi-transparent background matching HudOverlay aesthetic
Item {
    id: approvalDialog
    anchors.fill: parent

    // CRITICAL: use opacity > 0, not approvalPending directly
    // Prevents mouse-event blocking when faded out (RESEARCH.md Pitfall 3)
    visible: opacity > 0
    opacity: connectionBridge.approvalPending ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: 150 } }

    // Full-screen semi-transparent backdrop — dims the content behind the dialog
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
    }

    // Full-screen MouseArea — blocks all input to underlying content when dialog is visible.
    // acceptedButtons: Qt.AllButtons ensures no click passes through (D-13).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        // Consume events — do not propagate to content below
    }

    // Centered dialog card
    Rectangle {
        id: dialogCard
        anchors.centerIn: parent
        width: 420
        height: 220
        radius: 12
        color: "#CC1A1A2E"

        Column {
            anchors.centerIn: parent
            spacing: 12
            width: parent.width - 48

            // Title
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Connection Request"
                color: "white"
                font.pixelSize: 20
                font.bold: true
                font.family: "sans-serif"
                horizontalAlignment: Text.AlignHCenter
            }

            // Device name + protocol
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: connectionBridge.pendingDeviceName + " (" + connectionBridge.pendingProtocol + ")"
                color: "white"
                font.pixelSize: 16
                font.family: "sans-serif"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                width: parent.width
            }

            // Subtitle
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "wants to connect to this display"
                color: "#BBBBBB"
                font.pixelSize: 14
                font.family: "sans-serif"
                horizontalAlignment: Text.AlignHCenter
            }

            // Allow / Deny buttons
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16

                // Allow button
                Item {
                    id: allowButton
                    width: 130
                    height: 40

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: allowMouse.containsMouse ? "#55CC55" : "#44AA44"
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Allow"
                        color: "white"
                        font.pixelSize: 16
                        font.bold: true
                        font.family: "sans-serif"
                    }

                    MouseArea {
                        id: allowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            securityManager.resolveApproval(connectionBridge.pendingRequestId, true)
                            connectionBridge.clearApprovalRequest()
                        }
                    }
                }

                // Deny button
                Item {
                    id: denyButton
                    width: 130
                    height: 40

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: denyMouse.containsMouse ? "#CC5555" : "#AA4444"
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Deny"
                        color: "white"
                        font.pixelSize: 16
                        font.bold: true
                        font.family: "sans-serif"
                    }

                    MouseArea {
                        id: denyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            securityManager.resolveApproval(connectionBridge.pendingRequestId, false)
                            connectionBridge.clearApprovalRequest()
                        }
                    }
                }
            }
        }
    }
}
