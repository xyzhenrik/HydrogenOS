import QtQuick

Rectangle {
    id: root

    property int materialLevel: Tokens.materialFull
    property bool reduceMotion: false
    property bool interactiveHighlight: true

    color: materialLevel === Tokens.materialOpaque
           ? Tokens.glassOpaque
           : materialLevel === Tokens.materialEfficient
             ? Tokens.glassEfficient
             : Tokens.glassFull
    radius: Tokens.radiusLarge
    border.width: 1
    border.color: Tokens.glassBorder
    antialiasing: true

    Behavior on color {
        enabled: !root.reduceMotion
        ColorAnimation { duration: Tokens.durationNormal }
    }

    ShaderEffect {
        objectName: "glassHighlight"
        property bool materialEnabled: root.materialLevel === Tokens.materialFull
                                       && root.interactiveHighlight
        anchors.fill: parent
        visible: materialEnabled
        opacity: 0.52
        fragmentShader: "qrc:/qt/qml/Hydrogen/Design/shaders/glass-highlight.frag.qsb"
        property vector2d resolution: Qt.vector2d(width, height)
        property real strength: 0.8
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, parent.radius - 1)
        color: "transparent"
        border.width: 1
        border.color: "#16ffffff"
    }
}
