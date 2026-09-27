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

    QtObject {
        id: backend

        property bool available: true
        property bool busy: false
        property string materialQuality: "full"
        property bool reduceMotion: true
        property bool reduceTransparency: false
        property string errorMessage: ""
        property int motionWrites: 0
        property int transparencyWrites: 0

        function setReduceMotion(enabled) {
            motionWrites += 1
            reduceMotion = enabled
        }

        function setReduceTransparency(enabled) {
            transparencyWrites += 1
            reduceTransparency = enabled
        }

        function reset() {
            available = true
            busy = false
            materialQuality = "full"
            reduceMotion = true
            reduceTransparency = false
            errorMessage = ""
            motionWrites = 0
            transparencyWrites = 0
        }
    }

    function init() {
        backend.reset()
        const component = Qt.createComponent(Qt.resolvedUrl("../../shell/Main.qml"))
        compare(component.status, Component.Ready, component.errorString())
        shellWindow = component.createObject(null, {
            clockText: "09:41",
            settingsBackend: backend
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

        backend.materialQuality = "efficient"
        tryCompare(shellWindow, "glassLevel", Tokens.materialEfficient)

        mouseClick(transparency)
        tryCompare(shellWindow, "reduceTransparency", true)
        compare(backend.transparencyWrites, 1)
        compare(shellWindow.glassLevel, Tokens.materialOpaque)
        compare(surface.materialLevel, Tokens.materialOpaque)
        const highlight = findChild(surface, "glassHighlight")
        verify(highlight !== null)
        compare(highlight.materialEnabled, false)

        mouseClick(motion)
        tryCompare(shellWindow, "reduceMotion", false)
        compare(backend.motionWrites, 1)
        compare(center.transitionDuration, Tokens.durationNormal)
    }

    function test_unavailableBackendExplainsAndDisablesControls() {
        backend.available = false
        backend.errorMessage = "test service stopped"

        const button = findChild(shellWindow, "controlCenterButton")
        const transparency = findChild(shellWindow, "controlCenterTransparency")
        const motion = findChild(shellWindow, "controlCenterMotion")
        mouseClick(button)

        tryCompare(transparency, "enabled", false)
        compare(motion.enabled, false)
        const center = findChild(shellWindow, "controlCenter")
        verify(center.statusText.indexOf("test service stopped") !== -1)
    }
}
