pragma Singleton

import QtQuick

QtObject {
    readonly property int materialFull: 0
    readonly property int materialEfficient: 1
    readonly property int materialOpaque: 2

    readonly property color canvasTop: "#101827"
    readonly property color canvasBottom: "#071018"
    readonly property color textPrimary: "#f4f8ff"
    readonly property color textSecondary: "#aebbd0"
    readonly property color accent: "#66d7ff"
    readonly property color accentStrong: "#31bde9"
    readonly property color danger: "#ff6f7d"
    readonly property color focus: "#8de4ff"

    readonly property color glassFull: "#5c7992a8"
    readonly property color glassEfficient: "#c0283b4d"
    readonly property color glassOpaque: "#f0172432"
    readonly property color glassBorder: "#55d8efff"

    readonly property int radiusSmall: 10
    readonly property int radiusMedium: 18
    readonly property int radiusLarge: 28
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space6: 24
    readonly property int space8: 32

    readonly property int durationFast: 110
    readonly property int durationNormal: 180
    readonly property int durationSlow: 280
}

