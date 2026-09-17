# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- required, not decorative. This file runs under `cmake -P`
# (script mode), which does NOT inherit the root CMakeLists.txt's
# cmake_minimum_required. Without a floor here the script starts with every
# policy unset, and CMake's behaviour then depends on the CMake build on the
# machine: `if(x IN_LIST list)` (CMP0057, introduced in 3.3) is a hard
# "Unknown arguments specified" error under OLD. That is exactly how CI's
# ubuntu-latest job went red on 2026-09-06 while windows and macos passed --
# same script, different CMake, different policy defaults. Keep this line.
cmake_minimum_required(VERSION 3.22)
# Fails if any translation unit pulls in a GUI or audio-device framework.
# Keeping a layer free of them is what makes it testable offline. Shared
# between rta_core (the default), rta_platform_types, and the JUCE-free half
# of app/ -- pass LABEL to name the layer being checked in the messages below.
#
# Two ways to select what gets scanned; exactly one must be given:
#   CORE_DIR  the include/src/tests convention rta_core and rta_platform_types
#             both follow -- everything under CORE_DIR/{include,src,tests}.
#             tests/ is scanned for headers as well as .cpp: until 2026-09-16
#             it was .cpp only, so four committed fixture headers under
#             core/tests/ could have included JUCE unreported.
#   GLOBS     an explicit, semicolon-separated list of files (or glob
#             patterns), for a layer with no such directory convention.
#             app/src/measure is the reason this exists: it sits flat, and it
#             is NOT entirely JUCE-free by design (AnalysisThread and
#             SyntheticInput use juce::Thread on purpose), so a caller there
#             must name files rather than hand this script a directory.
if(NOT DEFINED CORE_DIR AND NOT DEFINED GLOBS)
    message(FATAL_ERROR "Neither CORE_DIR nor GLOBS is set")
endif()

if(NOT DEFINED LABEL)
    set(LABEL "rta_core")
endif()

if(DEFINED GLOBS)
    file(GLOB_RECURSE sources ${GLOBS})
else()
    file(GLOB_RECURSE sources
        "${CORE_DIR}/include/*.h"
        "${CORE_DIR}/include/*.hpp"
        "${CORE_DIR}/src/*.h"
        "${CORE_DIR}/src/*.cpp"
        "${CORE_DIR}/tests/*.cpp"
        "${CORE_DIR}/tests/*.h"
        "${CORE_DIR}/tests/*.hpp"
    )
endif()

set(violations "")
foreach(file IN LISTS sources)
    file(READ "${file}" content)
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](juce|JuceHeader|Q[A-Z]|portaudio|RtAudio|asio)")
        list(APPEND violations "${file}")
    endif()
endforeach()

list(LENGTH sources n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "No sources found for ${LABEL} -- check is not actually running")
endif()

if(violations)
    message(FATAL_ERROR
        "${LABEL} must not depend on a GUI/audio framework. Offending files:\n"
        "  ${violations}")
endif()

message(STATUS "${LABEL}_has_no_framework_deps: OK (${n_sources} files scanned)")
