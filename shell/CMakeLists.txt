qt_add_executable(hydrogen-shell main.cpp)

qt_add_qml_module(hydrogen-shell
    URI Hydrogen.Shell
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
)

target_link_libraries(hydrogen-shell PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    hydrogen-designplugin
)

install(TARGETS hydrogen-shell RUNTIME DESTINATION bin)

add_test(NAME shell-smoke COMMAND hydrogen-shell)
set_tests_properties(shell-smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HYDROGEN_SMOKE_TEST=1"
    TIMEOUT 10
)
