import QtQuick
import QtQuick.Controls
import QtTest
import Hydrogen.Design

TestCase {
    id: testCase

    name: "HydrogenSettings"
    width: 1024
    height: 720
    when: windowShown

    property var settingsWindow: null

    QtObject {
        id: backend

        property bool available: true
        property bool busy: false
        property int schemaVersion: 1
        property string materialQuality: "full"
        property bool reduceMotion: false
        property bool reduceTransparency: false
        property string errorMessage: ""
        property int materialWrites: 0
        property int motionWrites: 0
        property int transparencyWrites: 0

        function setMaterialQuality(quality) {
            materialWrites += 1
            materialQuality = quality
        }

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
            reduceMotion = false
            reduceTransparency = false
            errorMessage = ""
            materialWrites = 0
            motionWrites = 0
            transparencyWrites = 0
        }
    }

    function init() {
        backend.reset()
        const component = Qt.createComponent(Qt.resolvedUrl("../../settings/Main.qml"))
        compare(component.status, Component.Ready, component.errorString())
        settingsWindow = component.createObject(null, { settingsBackend: backend })
        verify(settingsWindow !== null, component.errorString())
        settingsWindow.show()
        wait(30)
    }

    function cleanup() {
        settingsWindow.destroy()
        settingsWindow = null
    }

    function test_backendStateControlsAppearance() {
        compare(settingsWindow.reduceMotion, false)
        compare(settingsWindow.materialLevel, Tokens.materialFull)

        backend.materialQuality = "efficient"
        tryCompare(settingsWindow, "materialLevel", Tokens.materialEfficient)

        backend.reduceTransparency = true
        tryCompare(settingsWindow, "materialLevel", Tokens.materialOpaque)
        const materialGroup = findChild(settingsWindow, "materialQualityGroup")
        verify(materialGroup !== null)
        compare(materialGroup.enabled, false)
    }

    function test_keyboardToggleWritesThroughBackend() {
        const motionToggle = findChild(settingsWindow, "motionToggle")
        verify(motionToggle !== null)
        motionToggle.forceActiveFocus()
        tryCompare(motionToggle, "activeFocus", true)

        keyClick(Qt.Key_Space)
        tryCompare(backend, "reduceMotion", true)
        compare(backend.motionWrites, 1)
    }

    function test_materialSelectionWritesThroughBackend() {
        const efficient = findChild(settingsWindow, "materialEfficient")
        verify(efficient !== null)
        mouseClick(efficient)
        tryCompare(backend, "materialQuality", "efficient")
        compare(backend.materialWrites, 1)
    }

    function test_unavailableBackendDisablesControls() {
        backend.available = false
        backend.errorMessage = "test service stopped"

        const motionToggle = findChild(settingsWindow, "motionToggle")
        const transparencyToggle = findChild(settingsWindow, "transparencyToggle")
        const status = findChild(settingsWindow, "backendStatus")
        tryCompare(motionToggle, "enabled", false)
        compare(transparencyToggle.enabled, false)
        verify(status.text.indexOf("test service stopped") !== -1)
        compare(status.Accessible.name, status.text)
    }
}
