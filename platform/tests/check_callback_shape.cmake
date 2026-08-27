# SPDX-License-Identifier: AGPL-3.0-or-later
#
# ctest `audioio_scoped_no_denormals_is_first` (plan §4.3, §9.1). Fails if the
# FIRST statement inside AudioIo::audioDeviceIOCallbackWithContext is not
# `juce::ScopedNoDenormals`. Registered only when RTA_BUILD_APP is ON --
# platform/src/AudioIo.cpp does not exist in a CI-configured (RTA_BUILD_APP=
# OFF) tree.
#
# Why this is worth a dedicated test rather than a review checklist item: the
# decision record traces a real production incident (handsfree's AudioEngine)
# where a subnormal limit cycle in filter state cost ~80x CPU -- 77 ms of CPU
# per second of audio versus 0.9 ms -- invisible in a quiet room and fatal at
# a live show. ScopedNoDenormals must run before ANY other statement touches
# a float, or the flush-to-zero flags it sets are too late for whatever ran
# first.
#
# Mechanism: no C++ parser here, just brace counting over the file's raw
# lines. IMPORTANT -- do not `list(APPEND)` raw source lines into a second
# CMake list: a CMake list is fundamentally a semicolon-joined string, so a
# line ending in a statement's `;` (i.e. almost every C++ line) gets silently
# re-split into extra elements the moment it is APPENDed to another list
# variable. `file(STRINGS ...)`'s own output does not have this problem (each
# physical line is already one element), so every step below reads the
# original `lines` list by index with `list(GET ...)` and never rebuilds it.
#
# ENCODING UTF-8 is required, not cosmetic: this codebase's comments use `§`
# (a project convention -- see CaptureBus.h, core/src/gen/Synthetic.cpp).
# Without it, file(STRINGS) treats the multi-byte UTF-8 sequence as a
# non-printable separator and silently SPLITS that one physical line into two
# list elements at the `§` -- the tail half then looks like its own "line"
# that does not start with `//`, which broke this check's comment filter the
# first time it ran against real source (proof this guard actually reads the
# file it is pointed at, not a fixture written to make it pass).
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_callback_shape: SOURCE not set")
endif()

if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "check_callback_shape: no such file: ${SOURCE}")
endif()

file(STRINGS "${SOURCE}" lines ENCODING UTF-8)
list(LENGTH lines total_lines)

if(total_lines EQUAL 0)
    message(FATAL_ERROR "check_callback_shape: ${SOURCE} is empty")
endif()

# Find the LAST line mentioning the function name that is not a `//` comment
# -- the definition, not a declaration or a doc-comment reference to it.
set(sig_index -1)
math(EXPR last_line "${total_lines} - 1")
foreach(i RANGE 0 ${last_line})
    list(GET lines ${i} line)
    if(line MATCHES "audioDeviceIOCallbackWithContext" AND NOT line MATCHES "^[ \t]*//")
        set(sig_index ${i})
    endif()
endforeach()

if(sig_index EQUAL -1)
    message(FATAL_ERROR
        "check_callback_shape: audioDeviceIOCallbackWithContext not found in ${SOURCE}")
endif()

# Walk forward from the signature, tracking brace depth per line, to find the
# body: the first '{' opens it (possibly several parameter lines later, since
# the signature spans multiple lines), and depth returning to 0 closes it.
set(depth 0)
set(seen_open FALSE)
set(body_start -1)
set(body_end -1)
set(i ${sig_index})
while(i LESS total_lines)
    list(GET lines ${i} line)
    string(REGEX MATCHALL "{" opens "${line}")
    string(REGEX MATCHALL "}" closes "${line}")
    list(LENGTH opens n_open)
    list(LENGTH closes n_close)

    if(NOT seen_open AND n_open GREATER 0)
        set(seen_open TRUE)
        set(body_start ${i})
    endif()

    math(EXPR depth "${depth} + ${n_open} - ${n_close}")

    if(seen_open AND depth EQUAL 0)
        set(body_end ${i})
        break()
    endif()

    math(EXPR i "${i} + 1")
endwhile()

if(NOT seen_open OR body_end EQUAL -1)
    message(FATAL_ERROR
        "check_callback_shape: could not find the closing brace of "
        "audioDeviceIOCallbackWithContext's body in ${SOURCE} (unbalanced braces?)")
endif()

# The first non-blank, non-comment line strictly between the opening-brace
# line and the closing-brace line is the function's first statement.
set(first_statement "")
math(EXPR first_body_line "${body_start} + 1")
math(EXPR last_body_line "${body_end} - 1")
if(first_body_line LESS_EQUAL last_body_line)
    foreach(j RANGE ${first_body_line} ${last_body_line})
        list(GET lines ${j} candidate)
        string(STRIP "${candidate}" stripped)
        if(stripped STREQUAL "")
            continue()
        endif()
        if(stripped MATCHES "^//")
            continue()
        endif()
        set(first_statement "${stripped}")
        break()
    endforeach()
endif()

if(first_statement STREQUAL "")
    message(FATAL_ERROR
        "check_callback_shape: audioDeviceIOCallbackWithContext's body in "
        "${SOURCE} appears to be empty")
endif()

if(NOT first_statement MATCHES "ScopedNoDenormals")
    message(FATAL_ERROR
        "check_callback_shape: the FIRST statement in "
        "AudioIo::audioDeviceIOCallbackWithContext must be "
        "juce::ScopedNoDenormals (decision record / plan §9.1 -- a subnormal "
        "limit cycle costs ~80x CPU). Found instead: ${first_statement}")
endif()

message(STATUS "audioio_scoped_no_denormals_is_first: OK (${first_statement})")
