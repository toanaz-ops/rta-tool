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

# app/src/measure/AtomicSharedPtr.h has two implementations and compiles the
# one this standard library supports. The fallback is the branch ONLY Apple
# libc++ takes, which makes it the branch nobody developing on Windows or
# Linux can break and notice. Turning this on compiles it anywhere:
#
#   cmake -S . -B build-fallback -DRTA_BUILD_APP=OFF \
#         -DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON
#   cmake --build build-fallback --config Release --parallel 4
#   ctest --test-dir build-fallback -C Release --output-on-failure
#
# Never turn it on for a shipping build: it opts into functions the standard
# removes in C++26 and buys nothing where the specialisation exists.
option(RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK
       "Compile AtomicSharedPtr's libc++ fallback even where std::atomic<shared_ptr> exists" OFF)

if(RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
    # GLOBAL, not per target, and that is the whole point. The macro changes
    # AtomicSharedPtr's member TYPE, so it changes the layout of every class
    # that holds one (Analyser, AnalysisThread). Defined in some translation
    # units and not others it is an ODR violation that links without a murmur
    # and misbehaves at run time -- far worse than the portability bug it
    # exists to test for. All targets or none.
    add_compile_definitions(RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
    message(STATUS "AtomicSharedPtr: forcing the std::atomic_* free-function fallback")
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Vendored server library -------------------------------------------
# cpp-httplib v0.56.0, the amalgamated header, MIT. See
# external/cpp-httplib/PROVENANCE.md for the tag, the SHA-256 and why the
# amalgamated file rather than split.py output (record
# docs/dsp/2026-09-16-remote-api.md sec.15 R2).
#
# OUTSIDE the if(RTA_BUILD_APP) block, and that placement is the whole of
# sec.15 R15: `ApiServer` runs on a std::thread rather than a juce::Thread, so
# the server and its Host check are built and tested in the RTA_BUILD_APP=OFF
# configuration -- the only one CI runs, on three operating systems. Inside the
# guard, the network layer would be proven on zero CI machines.
#
# An INTERFACE target rather than a bare include directory: app/src/api/
# ApiServer.cpp is the ONE permitted includer (core/tests/
# check_no_server_library.cmake), and it includes `<httplib.h>` -- so
# external/cpp-httplib itself has to be on the include path, not external/.
# The vendored header is NOT marked SYSTEM: with `#pragma warning(push, 0)`
# around the include in ApiServer.cpp the /W4 count stays at zero, measured
# before and after (sec.15 R7). SYSTEM + /external:W0 is the fallback if a
# later upstream version needs it; it was not needed at v0.56.0.
#
# Every CPPHTTPLIB_* backend macro is left undefined -- no TLS, no zlib, no
# brotli, no zstd, no NO_EXCEPTIONS. Deliberately NOT restated as
# target_compile_definitions: a list of macros we do not define is prose, and
# it lives in PROVENANCE.md where the reasons are.
add_library(rta_httplib INTERFACE)
target_include_directories(rta_httplib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-httplib)
if(WIN32)
    # cpp-httplib is the first thing in this repository to open a socket, and
    # Winsock is not linked by default. rtatool_analysis_tests (RTA_BUILD_APP
    # =OFF) had never needed ws2_32 before, so this is where it arrives.
    target_link_libraries(rta_httplib INTERFACE ws2_32)
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
