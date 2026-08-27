# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests for the JUCE-free half of platform/ (rta_platform_types). Built
# whenever RTA_BUILD_TESTS is on, unconditionally of RTA_BUILD_APP -- see
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1.
add_executable(rta_platform_tests
    test_channel_config.cpp
    test_capture_bus.cpp
)

target_link_libraries(rta_platform_tests PRIVATE
    rta::platform_types Catch2::Catch2WithMain)

# No `include(Catch)` here: that CMake module -- and the CMAKE_MODULE_PATH
# entry it needs -- is loaded once by core/tests/CMakeLists.txt, and CMake
# function/macro definitions (unlike variables) are global for the rest of
# the configure run, not directory-scoped. Root CMakeLists.txt keeps
# add_subdirectory(core/tests) ordered before this directory for exactly
# this reason.
catch_discover_tests(rta_platform_tests)

# --- Architectural guard ---------------------------------------------------
# rta_platform_types must stay as JUCE-free as rta_core. Reuses core's guard
# script, parameterized with LABEL so the failure message names the right
# layer. Registered unconditionally: platform/types builds even when
# RTA_BUILD_APP is OFF, so this must be provable on CI too.
add_test(NAME platform_types_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../types
            -DLABEL=rta_platform_types
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_framework_deps.cmake)

# --- Callback-shape guard ---------------------------------------------------
# Only registered when RTA_BUILD_APP is ON: the file it greps
# (platform/src/AudioIo.cpp) is not present in a CI-configured
# (RTA_BUILD_APP=OFF) tree. See check_callback_shape.cmake and
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §4.3.
if(RTA_BUILD_APP)
    add_test(NAME audioio_scoped_no_denormals_is_first
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE=${CMAKE_SOURCE_DIR}/platform/src/AudioIo.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_callback_shape.cmake)
endif()
