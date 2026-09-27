import QtQuick
import QtQuick.Controls
import Hydrogen.Design

GlassSurface {
    id: root

    objectName: "applicationDock"
    width: 420
    height: 70
    radius: 24

    property var applications: [
        { "appId": "org.kde.dolphin", "name": qsTr("Files"), "monogram": "F" },
        { "appId": "org.mozilla.firefox", "name": qsTr("Web"), "monogram": "W" },
        { "appId": "org.hydrogen.Settings", "name": qsTr("Settings"), "monogram": "S" },
        { "appId": "org.kde.konsole", "name": qsTr("Terminal"), "monogram": "T" }
    ]
    property int focusedIndex: 0
    readonly property int motionDuration: reduceMotion ? 0 : Tokens.durationFast

    signal applicationActivated(string appId)

    function focusApplication(index) {
        if (applications.length === 0)
            return
        const wrappedIndex = (index + applications.length) % applications.length
        focusedIndex = wrappedIndex
        const item = applicationRepeater.itemAt(wrappedIndex)
        if (item !== null)
            item.forceActiveFocus(Qt.TabFocusReason)
    }

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Applications")

    Row {
        anchors.centerIn: parent
        spacing: Tokens.space3

        Repeater {
            id: applicationRepeater
            objectName: "dockApplications"
            model: root.applications

            delegate: Button {
                id: applicationButton

                required property int index
                required property var modelData

                width: 48
                height: 48
                text: modelData.monogram
                hoverEnabled: true
                scale: hovered && !root.reduceMotion ? 1.04 : 1

                Accessible.role: Accessible.Button
                Accessible.name: modelData.name
                Accessible.description: qsTr("Request application launch")

                contentItem: Text {
                    text: applicationButton.text
                    color: Tokens.textPrimary
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 15
                    color: applicationButton.down ? "#6631bde9"
                          : applicationButton.hovered ? "#4d66d7ff"
                          : "#2effffff"
                    border.color: applicationButton.activeFocus
                                  ? Tokens.focus
                                  : "#28ffffff"
                    border.width: applicationButton.activeFocus ? 2 : 1

                    Behavior on color {
                        enabled: !root.reduceMotion
                        ColorAnimation { duration: Tokens.durationFast }
                    }
                }

                Behavior on scale {
                    NumberAnimation {
                        duration: root.motionDuration
                        easing.type: Easing.OutCubic
                    }
                }

                onActiveFocusChanged: {
                    if (activeFocus)
                        root.focusedIndex = index
                }
                onClicked: root.applicationActivated(modelData.appId)
                Keys.onReturnPressed: root.applicationActivated(modelData.appId)
                Keys.onEnterPressed: root.applicationActivated(modelData.appId)
                Keys.onLeftPressed: root.focusApplication(index - 1)
                Keys.onRightPressed: root.focusApplication(index + 1)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home) {
                        root.focusApplication(0)
                        event.accepted = true
                    } else if (event.key === Qt.Key_End) {
                        root.focusApplication(root.applications.length - 1)
                        event.accepted = true
                    }
                }
            }
        }
    }
}
