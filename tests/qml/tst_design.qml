import QtQuick
import QtQuick.Controls
import QtTest
import Hydrogen.Design

TestCase {
    id: testCase

    name: "HydrogenDesign"
    width: 640
    height: 480
    when: windowShown

    Component {
        id: surfaceComponent

        GlassSurface {
            width: 320
            height: 180
        }
    }

    Component {
        id: buttonComponent

        HydrogenButton {
            text: "Open settings"
        }
    }

    function createTemporary(component, properties) {
        const object = component.createObject(testCase, properties || {})
        verify(object !== null)
        return object
    }

    function test_materialLevels_data() {
        return [
            { tag: "full", level: Tokens.materialFull, color: Tokens.glassFull, highlight: true },
            { tag: "efficient", level: Tokens.materialEfficient, color: Tokens.glassEfficient, highlight: false },
            { tag: "opaque", level: Tokens.materialOpaque, color: Tokens.glassOpaque, highlight: false }
        ]
    }

    function test_materialLevels(data) {
        const surface = createTemporary(surfaceComponent, {
            materialLevel: data.level,
            reduceMotion: true
        })
        compare(surface.color, data.color)
        const highlight = findChild(surface, "glassHighlight")
        verify(highlight !== null)
        compare(highlight.materialEnabled, data.highlight)
        surface.destroy()
    }

    function test_reduceTransparencyUsesOpaqueSurface() {
        const component = Qt.createComponent(Qt.resolvedUrl("../../shell/Main.qml"))
        compare(component.status, Component.Ready, component.errorString())
        const shell = component.createObject(null, {
            visible: false,
            reduceMotion: true,
            clockText: "09:41"
        })
        verify(shell !== null, component.errorString())

        shell.reduceTransparency = true
        compare(shell.glassLevel, Tokens.materialOpaque)
        const welcome = findChild(shell, "welcomeSurface")
        verify(welcome !== null)
        compare(welcome.materialLevel, Tokens.materialOpaque)
        const highlight = findChild(welcome, "glassHighlight")
        verify(highlight !== null)
        compare(highlight.materialEnabled, false)
        shell.destroy()
    }

    function test_reduceMotionDisablesColorAnimation() {
        const surface = createTemporary(surfaceComponent, {
            materialLevel: Tokens.materialFull,
            reduceMotion: true
        })
        surface.materialLevel = Tokens.materialOpaque
        compare(surface.color, Tokens.glassOpaque)

        surface.reduceMotion = false
        surface.materialLevel = Tokens.materialFull
        wait(20)
        verify(surface.color !== Tokens.glassFull)
        tryCompare(surface, "color", Tokens.glassFull, Tokens.durationNormal + 150)
        surface.destroy()
    }

    function test_buttonKeyboardFocusAndActivation() {
        const button = createTemporary(buttonComponent)
        let activations = 0
        button.clicked.connect(function() { activations += 1 })

        button.forceActiveFocus()
        tryCompare(button, "activeFocus", true)
        keyClick(Qt.Key_Space)
        compare(activations, 1)
        button.destroy()
    }

    function test_buttonAccessibilityMetadata() {
        const button = createTemporary(buttonComponent)
        compare(button.Accessible.role, Accessible.Button)
        compare(button.Accessible.name, "Open settings")
        button.destroy()
    }
}
