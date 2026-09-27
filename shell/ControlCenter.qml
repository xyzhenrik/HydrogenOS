import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hydrogen.Design

Popup {
    id: root

    objectName: "controlCenter"
    width: 320
    height: 286
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property bool reduceMotion: false
    property bool reduceTransparency: false
    property int materialLevel: Tokens.materialFull
    property bool controlsEnabled: true
    property string statusText: ""
    readonly property int transitionDuration: reduceMotion
                                              ? 0
                                              : Tokens.durationNormal

    signal reduceMotionRequested(bool enabled)
    signal reduceTransparencyRequested(bool enabled)

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: root.transitionDuration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"
            from: root.reduceMotion ? 1 : 0.98
            to: 1
            duration: root.transitionDuration
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: root.transitionDuration
            easing.type: Easing.InCubic
        }
    }

    onOpened: Qt.callLater(function() {
        transparencyToggle.forceActiveFocus()
    })

    background: GlassSurface {
        objectName: "controlCenterSurface"
        radius: Tokens.radiusMedium
        materialLevel: root.reduceTransparency
                       ? Tokens.materialOpaque
                       : root.materialLevel
        reduceMotion: root.reduceMotion
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Control Center")
    }

    contentItem: ColumnLayout {
        spacing: Tokens.space4

        Item { Layout.preferredHeight: Tokens.space4 }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.space4
            Layout.rightMargin: Tokens.space4

            Text {
                text: qsTr("Control Center")
                color: Tokens.textPrimary
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("Display")
                color: Tokens.textSecondary
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.space4
            Layout.rightMargin: Tokens.space4
            height: 1
            color: "#28ffffff"
        }

        CheckBox {
            id: transparencyToggle
            objectName: "controlCenterTransparency"
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.space4
            Layout.rightMargin: Tokens.space4
            text: qsTr("Reduce transparency")
            checked: root.reduceTransparency
            enabled: root.controlsEnabled
            onToggled: {
                if (checked !== root.reduceTransparency)
                    root.reduceTransparencyRequested(checked)
            }
            Accessible.description: qsTr("Uses opaque shell surfaces and disables glass highlights")
        }

        CheckBox {
            id: motionToggle
            objectName: "controlCenterMotion"
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.space4
            Layout.rightMargin: Tokens.space4
            text: qsTr("Reduce motion")
            checked: root.reduceMotion
            enabled: root.controlsEnabled
            onToggled: {
                if (checked !== root.reduceMotion)
                    root.reduceMotionRequested(checked)
            }
            Accessible.description: qsTr("Disables non-essential shell animations")
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.space4
            Layout.rightMargin: Tokens.space4
            Layout.bottomMargin: Tokens.space4
            text: root.statusText.length > 0
                  ? root.statusText
                  : qsTr("System controls will arrive as their M2 integrations become available.")
            wrapMode: Text.WordWrap
            color: Tokens.textSecondary
            font.pixelSize: 12
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
