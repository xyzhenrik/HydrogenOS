qt_add_executable(hydrogen-qml-tests tst_main.cpp)

target_link_libraries(hydrogen-qml-tests PRIVATE
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
    Qt6::Test
    hydrogen-designplugin
)

add_test(
    NAME design-qml-tests
    COMMAND hydrogen-qml-tests -input "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(design-qml-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 30
)
