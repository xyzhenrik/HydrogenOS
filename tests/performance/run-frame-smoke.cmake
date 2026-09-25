# SPDX-License-Identifier: GPL-3.0-or-later

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        QT_QPA_PLATFORM=offscreen
        QSG_RHI_BACKEND=software
        "${SHELL_EXECUTABLE}"
        --benchmark-output "${OUTPUT_PATH}"
        --benchmark-refresh 60
        --benchmark-frames 30
        --benchmark-warmup 5
    RESULT_VARIABLE BENCHMARK_RESULT
    OUTPUT_VARIABLE BENCHMARK_STDOUT
    ERROR_VARIABLE BENCHMARK_STDERR
)
if(NOT BENCHMARK_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Benchmark smoke run failed (${BENCHMARK_RESULT})\n"
        "stdout:\n${BENCHMARK_STDOUT}\n"
        "stderr:\n${BENCHMARK_STDERR}")
endif()

file(READ "${OUTPUT_PATH}" RESULT_JSON)
string(JSON SCHEMA_VERSION GET "${RESULT_JSON}" schema_version)
string(JSON KIND GET "${RESULT_JSON}" kind)
string(JSON SCENARIO GET "${RESULT_JSON}" scenario)
string(JSON MEASURED_FRAMES GET "${RESULT_JSON}" samples measured_frames)
string(JSON P95_MS GET "${RESULT_JSON}" metrics p95_ms)
string(JSON P95_BUDGET_PASSED GET "${RESULT_JSON}" assessment p95_budget_passed)
string(JSON QUALIFICATION_ELIGIBLE GET "${RESULT_JSON}" qualification eligible)
string(JSON GRAPHICS_API GET "${RESULT_JSON}" environment graphics_api)

if(NOT SCHEMA_VERSION EQUAL 1
   OR NOT KIND STREQUAL "frame_time"
   OR NOT SCENARIO STREQUAL "prototype_card_and_dock_transition"
   OR NOT MEASURED_FRAMES EQUAL 30
   OR P95_MS LESS_EQUAL 0
   OR QUALIFICATION_ELIGIBLE)
    message(FATAL_ERROR "Invalid CI benchmark result: ${RESULT_JSON}")
endif()

if(NOT GRAPHICS_API STREQUAL "software")
    message(FATAL_ERROR "CI benchmark must use software rendering, got ${GRAPHICS_API}")
endif()
