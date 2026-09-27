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

    property var settingsBackend: null
    readonly property bool backendAvailable: settingsBackend !== null
                                             && settingsBackend.available
    readonly property bool backendBusy: settingsBackend !== null
                                        && settingsBackend.busy
    readonly property bool reduceMotion: settingsBackend !== null
                                         && settingsBackend.reduceMotion
    readonly property bool reduceTransparency: settingsBackend !== null
                                               && settingsBackend.reduceTransparency
    readonly property string materialQuality: settingsBackend !== null
                                              ? settingsBackend.materialQuality
                                              : "full"
    readonly property string backendError: settingsBackend !== null
                                           ? settingsBackend.errorMessage
                                           : qsTr("No settings backend was provided.")
    property int materialLevel: reduceTransparency
                                ? Tokens.materialOpaque
                                : materialQuality === "efficient"
                                  ? Tokens.materialEfficient
                                  : materialQuality === "opaque"
                                    ? Tokens.materialOpaque
                                    : Tokens.materialFull

    ButtonGroup { id: materialGroup }

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
                    objectName: "transparencyToggle"
                    text: qsTr("Reduce transparency")
                    checked: root.reduceTransparency
                    enabled: root.backendAvailable && !root.backendBusy
                    onToggled: {
                        if (root.settingsBackend !== null
                                && checked !== root.settingsBackend.reduceTransparency)
                            root.settingsBackend.setReduceTransparency(checked)
                    }
                    Accessible.description: qsTr("Uses opaque surfaces and disables glass highlights")
                }

                CheckBox {
                    id: motionToggle
                    objectName: "motionToggle"
                    text: qsTr("Reduce motion")
                    checked: root.reduceMotion
                    enabled: root.backendAvailable && !root.backendBusy
                    onToggled: {
                        if (root.settingsBackend !== null
                                && checked !== root.settingsBackend.reduceMotion)
                            root.settingsBackend.setReduceMotion(checked)
                    }
                    Accessible.description: qsTr("Disables non-essential interface animations")
                }

                GroupBox {
                    objectName: "materialQualityGroup"
                    title: qsTr("Material quality")
                    Layout.fillWidth: true
                    enabled: root.backendAvailable && !root.backendBusy
                             && !root.reduceTransparency

                    RowLayout {
                        anchors.fill: parent

                        RadioButton {
                            objectName: "materialFull"
                            text: qsTr("Full")
                            ButtonGroup.group: materialGroup
                            checked: root.materialQuality === "full"
                            onClicked: root.settingsBackend.setMaterialQuality("full")
                        }
                        RadioButton {
                            objectName: "materialEfficient"
                            text: qsTr("Efficient")
                            ButtonGroup.group: materialGroup
                            checked: root.materialQuality === "efficient"
                            onClicked: root.settingsBackend.setMaterialQuality("efficient")
                        }
                        RadioButton {
                            objectName: "materialOpaque"
                            text: qsTr("Opaque")
                            ButtonGroup.group: materialGroup
                            checked: root.materialQuality === "opaque"
                            onClicked: root.settingsBackend.setMaterialQuality("opaque")
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    objectName: "backendStatus"
                    Layout.fillWidth: true
                    text: root.backendAvailable
                          ? qsTr("Changes are saved automatically.")
                          : qsTr("Settings service unavailable. %1").arg(root.backendError)
                    wrapMode: Text.WordWrap
                    color: root.backendAvailable ? Tokens.textSecondary : Tokens.focus
                    font.pixelSize: 13
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
    }
}
