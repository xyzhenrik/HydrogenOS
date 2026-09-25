import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hydrogen.Design

ApplicationWindow {
    id: root

    width: 980
    height: 680
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("Hydrogen Settings")
    color: Tokens.canvasBottom

    property bool reduceMotion: false
    property bool reduceTransparency: false
    property int materialLevel: reduceTransparency
                                ? Tokens.materialOpaque
                                : Tokens.materialFull

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Tokens.canvasTop }
            GradientStop { position: 1.0; color: Tokens.canvasBottom }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Tokens.space6
        spacing: Tokens.space4

        GlassSurface {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            radius: Tokens.radiusMedium
            materialLevel: root.materialLevel
            reduceMotion: root.reduceMotion

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Tokens.space4
                spacing: Tokens.space2

                Text {
                    text: qsTr("Settings")
                    color: Tokens.textPrimary
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: Tokens.space4
                }

                Repeater {
                    model: [
                        qsTr("Appearance"), qsTr("Displays"), qsTr("Sound"),
                        qsTr("Network"), qsTr("Bluetooth"), qsTr("Input"),
                        qsTr("Power"), qsTr("Privacy")
                    ]

                    delegate: HydrogenButton {
                        required property string modelData
                        text: modelData
                        Layout.fillWidth: true
                        reduceMotion: root.reduceMotion
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Tokens.radiusMedium
            materialLevel: root.materialLevel
            reduceMotion: root.reduceMotion

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Tokens.space8
                spacing: Tokens.space6

                SectionLabel { text: qsTr("Appearance") }

                Text {
                    text: qsTr("Comfort and clarity")
                    color: Tokens.textPrimary
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }

                CheckBox {
                    id: transparencyToggle
                    text: qsTr("Reduce transparency")
                    checked: root.reduceTransparency
                    onToggled: root.reduceTransparency = checked
                    Accessible.description: qsTr("Uses opaque surfaces and disables glass highlights")
                }

                CheckBox {
                    id: motionToggle
                    text: qsTr("Reduce motion")
                    checked: root.reduceMotion
                    onToggled: root.reduceMotion = checked
                    Accessible.description: qsTr("Disables non-essential interface animations")
                }

                GroupBox {
                    title: qsTr("Material quality")
                    Layout.fillWidth: true
                    enabled: !root.reduceTransparency

                    RowLayout {
                        anchors.fill: parent

                        RadioButton { text: qsTr("Full"); checked: true }
                        RadioButton { text: qsTr("Efficient") }
                        RadioButton { text: qsTr("Opaque") }
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Settings are local in this prototype. The versioned settings service is implemented separately and will be connected in M2.")
                    wrapMode: Text.WordWrap
                    color: Tokens.textSecondary
                    font.pixelSize: 13
                }
            }
        }
    }
}

