# SPDX-License-Identifier: AGPL-3.0-or-later
# Fails if any translation unit in rta_core pulls in a GUI or audio-device
# framework. Keeping the core free of them is what makes it testable offline.
if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR not set")
endif()

file(GLOB_RECURSE sources
    "${CORE_DIR}/include/*.h"
    "${CORE_DIR}/include/*.hpp"
    "${CORE_DIR}/src/*.h"
    "${CORE_DIR}/src/*.cpp"
    "${CORE_DIR}/tests/*.cpp"
)

set(violations "")
foreach(file IN LISTS sources)
    file(READ "${file}" content)
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](juce|JuceHeader|Q[A-Z]|portaudio|RtAudio|asio)")
        list(APPEND violations "${file}")
    endif()
endforeach()

list(LENGTH sources n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "No sources found under ${CORE_DIR} -- check is not actually running")
endif()

if(violations)
    message(FATAL_ERROR
        "rta_core must not depend on a GUI/audio framework. Offending files:\n"
        "  ${violations}")
endif()

message(STATUS "core_has_no_framework_deps: OK (${n_sources} files scanned)")
