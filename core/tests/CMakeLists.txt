# SPDX-License-Identifier: AGPL-3.0-or-later
include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(rta_core_tests
    test_window.cpp
    test_ringbuffer.cpp
    test_fft.cpp
    test_bands.cpp
    test_spectrum.cpp
    test_bandweights.cpp
)

# Golden vectors are committed, so the C++ suite needs neither Python nor
# NumPy. Regenerate them with tools/gen_golden.py and review the diff.
target_compile_definitions(rta_core_tests PRIVATE
    RTA_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_include_directories(rta_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rta_core_tests PRIVATE rta::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(rta_core_tests)

# --- Architectural guard -------------------------------------------------
# rta_core must stay free of GUI / audio-device frameworks. This is a real
# test, not a comment: it fails the build pipeline if anyone adds such an
# include to core/.
add_test(NAME core_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_framework_deps.cmake
)
