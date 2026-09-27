# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        QT_QPA_PLATFORM=offscreen
        QSG_RHI_BACKEND=software
        "${SHELL_EXECUTABLE}" --list-screens
    RESULT_VARIABLE LIST_RESULT
    OUTPUT_VARIABLE SCREEN_LIST
    ERROR_VARIABLE LIST_STDERR
)
if(NOT LIST_RESULT EQUAL 0 OR NOT SCREEN_LIST MATCHES "Hz")
    message(FATAL_ERROR
        "Screen listing failed (${LIST_RESULT})\n"
        "stdout:\n${SCREEN_LIST}\n"
        "stderr:\n${LIST_STDERR}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        QT_QPA_PLATFORM=offscreen
        QSG_RHI_BACKEND=software
        "${SHELL_EXECUTABLE}"
        --benchmark-output "${CMAKE_CURRENT_BINARY_DIR}/unexpected.json"
        --benchmark-screen hydrogen-screen-that-does-not-exist
    RESULT_VARIABLE INVALID_SCREEN_RESULT
)
if(INVALID_SCREEN_RESULT EQUAL 0)
    message(FATAL_ERROR "An unknown benchmark screen was accepted")
endif()
