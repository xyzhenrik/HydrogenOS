# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_executable(tst-settings-backend
    tst_settings_backend.cpp
)
target_link_libraries(tst-settings-backend PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
    hydrogen-settings-client
)

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
add_test(
    NAME settings-backend-dbus
    COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst-settings-backend>
)
set_tests_properties(settings-backend-dbus PROPERTIES TIMEOUT 20)
