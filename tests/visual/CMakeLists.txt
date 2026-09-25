qt_add_executable(hydrogen-visual-compare compare.cpp)
target_link_libraries(hydrogen-visual-compare PRIVATE Qt6::Gui)

set(visual_artifact_dir "${CMAKE_BINARY_DIR}/test-artifacts/visual")

foreach(scale_spec IN ITEMS "100|1.0" "150|1.5" "200|2.0")
    string(REPLACE "|" ";" scale_parts "${scale_spec}")
    list(GET scale_parts 0 scale_label)
    list(GET scale_parts 1 scale_factor)
    add_test(
        NAME "visual-shell-${scale_label}"
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/compare-shell.sh"
            "$<TARGET_FILE:hydrogen-shell>"
            "$<TARGET_FILE:hydrogen-visual-compare>"
            "${scale_factor}"
            "${scale_label}"
            "${CMAKE_CURRENT_SOURCE_DIR}/baselines/shell-${scale_label}.png"
            "${visual_artifact_dir}"
    )
    set_tests_properties("visual-shell-${scale_label}" PROPERTIES TIMEOUT 30)
endforeach()
