import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool reduceMotion: false

    implicitWidth: Math.max(112, contentItem.implicitWidth + Tokens.space8)
    implicitHeight: 42
    hoverEnabled: true

    Accessible.name: text
    Accessible.role: Accessible.Button

    contentItem: Text {
        text: control.text
        color: Tokens.textPrimary
        font.pixelSize: 14
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: Tokens.radiusSmall
        color: control.down ? "#6631bde9"
              : control.hovered ? "#4d66d7ff"
              : "#2effffff"
        border.color: control.activeFocus ? Tokens.focus : "#28ffffff"
        border.width: control.activeFocus ? 2 : 1

        Behavior on color {
            enabled: !control.reduceMotion
            ColorAnimation { duration: Tokens.durationFast }
        }
    }
}

