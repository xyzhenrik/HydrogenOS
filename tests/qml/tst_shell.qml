import QtQuick
import QtQuick.Controls
import QtTest
import Hydrogen.Design

TestCase {
    id: testCase

    name: "HydrogenShell"
    width: 1280
    height: 760
    when: windowShown

    property var shellWindow: null

    function init() {
        const component = Qt.createComponent(Qt.resolvedUrl("../../shell/Main.qml"))
        compare(component.status, Component.Ready, component.errorString())
        shellWindow = component.createObject(null, {
            clockText: "09:41",
            reduceMotion: true
        })
        verify(shellWindow !== null, component.errorString())
        shellWindow.show()
        wait(30)
    }

    function cleanup() {
        shellWindow.destroy()
        shellWindow = null
    }

    function test_controlCenterKeyboardAndFocus() {
        const button = findChild(shellWindow, "controlCenterButton")
        const center = findChild(shellWindow, "controlCenter")
        const transparency = findChild(shellWindow, "controlCenterTransparency")
        verify(button !== null)
        verify(center !== null)
        verify(transparency !== null)
        compare(button.Accessible.name, "Control Center")
        compare(center.Accessible.role, Accessible.Dialog)
        compare(center.Accessible.name, "Control Center")

        button.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(center, "opened", true)
        tryCompare(transparency, "activeFocus", true)

        keyClick(Qt.Key_Escape)
        tryCompare(center, "opened", false)
        tryCompare(button, "activeFocus", true)
    }

    function test_controlCenterUpdatesAccessibilityModes() {
        const button = findChild(shellWindow, "controlCenterButton")
        const center = findChild(shellWindow, "controlCenter")
        const surface = findChild(shellWindow, "controlCenterSurface")
        const transparency = findChild(shellWindow, "controlCenterTransparency")
        const motion = findChild(shellWindow, "controlCenterMotion")

        verify(surface !== null)
        compare(center.transitionDuration, 0)
        mouseClick(button)
        tryCompare(center, "opened", true)

        mouseClick(transparency)
        tryCompare(shellWindow, "reduceTransparency", true)
        compare(shellWindow.glassLevel, Tokens.materialOpaque)
        compare(surface.materialLevel, Tokens.materialOpaque)
        const highlight = findChild(surface, "glassHighlight")
        verify(highlight !== null)
        compare(highlight.materialEnabled, false)

        mouseClick(motion)
        tryCompare(shellWindow, "reduceMotion", false)
        compare(center.transitionDuration, Tokens.durationNormal)
    }
}
