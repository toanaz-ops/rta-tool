# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)

project(RtaTool
    VERSION 0.1.0
    DESCRIPTION "Dual-FFT audio measurement tool"
    # C is required: JUCE compiles C sources in several of its modules and
    # refuses to configure without the language enabled.
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RTA_BUILD_TESTS "Build rta_core unit tests" ON)
option(RTA_BUILD_APP   "Build the JUCE application shell" OFF)
option(RTA_ENABLE_ASIO "Enable ASIO support (Windows only)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Layers, bottom up -------------------------------------------------
# core      pure DSP, no framework          (portable)
# platform  audio device I/O behind an interface
# ui        az_ui JUCE module, design system (portable)
# app       composition root

add_subdirectory(core)

# platform/types is JUCE-free on purpose, so it builds and is tested on CI
# even when RTA_BUILD_APP is OFF. See
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1.
add_subdirectory(platform/types)

if(RTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)      # fetches Catch2; must stay first
    add_subdirectory(platform/tests)
    add_subdirectory(app/tests)       # JUCE-free half of app/; see §1.3
endif()

if(RTA_BUILD_APP)
    # JUCE is acquired here, not inside app/, because ui/ needs it too.
    #
    # RTA_JUCE_PATH lets a machine that already has a checkout reuse it rather
    # than clone a second ~200 MB copy. It is opt-in and explicit: silently
    # reaching for a sibling project's copy would make the build depend on a
    # path outside this repo without saying so.
    set(RTA_JUCE_PATH "" CACHE PATH "Existing JUCE checkout to reuse; empty = fetch")

    if(RTA_JUCE_PATH)
        message(STATUS "Reusing JUCE from ${RTA_JUCE_PATH}")
        add_subdirectory(${RTA_JUCE_PATH} ${CMAKE_BINARY_DIR}/_juce EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        9.0.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(JUCE)
    endif()

    # rta_platform is the one target allowed to link juce_audio_devices --
    # see platform/CMakeLists.txt and docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1.
    add_subdirectory(platform)

    # rta_platform_juce_tests needs rta::platform (JUCE), so it can only be
    # added here, inside RTA_BUILD_APP, unlike platform/tests (JUCE-free,
    # added unconditionally above). See platform/tests_juce/CMakeLists.txt's
    # header comment.
    if(RTA_BUILD_TESTS)
        add_subdirectory(platform/tests_juce)
    endif()

    add_subdirectory(ui)

    # az_ui_tests links az_ui and juce_gui_basics, so like rta_platform_juce_tests
    # above it can only be added inside RTA_BUILD_APP -- there is no JUCE-free
    # half of az_ui to test with RTA_BUILD_APP=OFF. Added AFTER
    # add_subdirectory(ui) so the az_ui module target is defined before
    # anything links it. Same pattern as app/tests_juce below.
    if(RTA_BUILD_TESTS)
        add_subdirectory(ui/tests)
    endif()

    add_subdirectory(app)

    # rtatool_view_tests links az_ui and juce_gui_basics, so like
    # rta_platform_juce_tests above it can only be added inside RTA_BUILD_APP --
    # unlike app/tests (JUCE-free, added unconditionally above), which must stay
    # buildable with RTA_BUILD_APP=OFF. Added AFTER add_subdirectory(ui) so the
    # az_ui module target is defined before anything links it. See
    # app/tests_juce/CMakeLists.txt's header comment.
    if(RTA_BUILD_TESTS)
        add_subdirectory(app/tests_juce)
    endif()
endif()
