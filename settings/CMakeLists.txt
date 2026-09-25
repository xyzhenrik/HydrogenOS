qt_add_executable(hydrogen-settings main.cpp)

qt_add_qml_module(hydrogen-settings
    URI Hydrogen.Settings
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
)

target_link_libraries(hydrogen-settings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    hydrogen-designplugin
)

install(TARGETS hydrogen-settings RUNTIME DESTINATION bin)

add_test(NAME settings-smoke COMMAND hydrogen-settings)
set_tests_properties(settings-smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HYDROGEN_SMOKE_TEST=1"
    TIMEOUT 10
)
