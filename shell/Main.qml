import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hydrogen.Design

ApplicationWindow {
    id: window

    width: 1280
    height: 760
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: qsTr("Hydrogen Shell Preview")
    color: Tokens.canvasBottom

    property bool reduceMotion: false
    property bool reduceTransparency: false
    property int glassLevel: reduceTransparency
                             ? Tokens.materialOpaque
                             : Tokens.materialFull

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Tokens.canvasTop }
            GradientStop { position: 0.48; color: "#12304a" }
            GradientStop { position: 1.0; color: Tokens.canvasBottom }
        }
    }

    Rectangle {
        width: 520
        height: 520
        radius: width / 2
        x: window.width * 0.48
        y: -190
        color: "#3046d8ff"
        opacity: 0.8
    }

    GlassSurface {
        id: panel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Tokens.space4
        height: 54
        radius: Tokens.radiusMedium
        materialLevel: window.glassLevel
        reduceMotion: window.reduceMotion

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tokens.space4
            anchors.rightMargin: Tokens.space4
            spacing: Tokens.space3

            Text {
                text: qsTr("Hydrogen")
                color: Tokens.textPrimary
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("Developer Preview")
                color: Tokens.textSecondary
                font.pixelSize: 13
            }

            Text {
                text: Qt.formatTime(new Date(), "hh:mm")
                color: Tokens.textPrimary
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }
    }

    GlassSurface {
        id: welcome
        width: Math.min(640, window.width - 96)
        height: 330
        anchors.centerIn: parent
        materialLevel: window.glassLevel
        reduceMotion: window.reduceMotion

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Tokens.space8
            spacing: Tokens.space4

            SectionLabel { text: qsTr("Design system prototype") }

            Text {
                Layout.fillWidth: true
                text: qsTr("A calm desktop, built to stay fast.")
                wrapMode: Text.WordWrap
                color: Tokens.textPrimary
                font.pixelSize: 34
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("This window validates Hydrogen's own material, motion, focus, and accessibility foundations. It is not the production shell yet.")
                wrapMode: Text.WordWrap
                color: Tokens.textSecondary
                font.pixelSize: 15
                lineHeight: 1.35
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                spacing: Tokens.space3

                HydrogenButton {
                    text: window.reduceTransparency
                          ? qsTr("Enable transparency")
                          : qsTr("Reduce transparency")
                    reduceMotion: window.reduceMotion
                    onClicked: window.reduceTransparency = !window.reduceTransparency
                }

                HydrogenButton {
                    text: window.reduceMotion
                          ? qsTr("Enable motion")
                          : qsTr("Reduce motion")
                    reduceMotion: window.reduceMotion
                    onClicked: window.reduceMotion = !window.reduceMotion
                }
            }
        }
    }

    GlassSurface {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Tokens.space6
        width: 420
        height: 70
        radius: 24
        materialLevel: window.glassLevel
        reduceMotion: window.reduceMotion

        Row {
            anchors.centerIn: parent
            spacing: Tokens.space3

            Repeater {
                model: ["Files", "Web", "Settings", "Terminal"]

                delegate: Rectangle {
                    required property string modelData
                    width: 48
                    height: 48
                    radius: 15
                    color: "#2effffff"
                    border.color: "#28ffffff"

                    Text {
                        anchors.centerIn: parent
                        text: modelData.substring(0, 1)
                        color: Tokens.textPrimary
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData
                }
            }
        }
    }
}

