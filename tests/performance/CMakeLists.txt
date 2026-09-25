# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_executable(tst-frame-metrics
    tst_frame_metrics.cpp
    ../../shell/FrameMetrics.cpp
    ../../shell/FrameMetrics.h
)
target_include_directories(tst-frame-metrics PRIVATE ../../shell)
target_link_libraries(tst-frame-metrics PRIVATE Qt6::Core Qt6::Test)

add_test(NAME frame-metrics-unit COMMAND tst-frame-metrics)
set_tests_properties(frame-metrics-unit PROPERTIES LABELS performance)

set(PERFORMANCE_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/test-artifacts/performance")
add_test(
    NAME shell-performance-smoke
    COMMAND
        ${CMAKE_COMMAND}
        -DSHELL_EXECUTABLE=$<TARGET_FILE:hydrogen-shell>
        -DOUTPUT_PATH=${PERFORMANCE_ARTIFACT_DIR}/ci-frame-smoke.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run-frame-smoke.cmake
)
set_tests_properties(shell-performance-smoke PROPERTIES LABELS performance TIMEOUT 30)

add_test(
    NAME idle-measurement-smoke
    COMMAND
        ${CMAKE_CURRENT_SOURCE_DIR}/test-idle-measurement.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/measure-idle.sh
        ${PERFORMANCE_ARTIFACT_DIR}/ci-idle-smoke.json
)
set_tests_properties(idle-measurement-smoke PROPERTIES LABELS performance TIMEOUT 10)
