# SPDX-License-Identifier: AGPL-3.0-or-later
#
# L7-OUT's "second tripwire" (decision record docs/dsp/2026-09-06-l7-output-
# path.md sec.7, sec.10 T11): a forbidden-token grep over ONE named
# function's body -- `new |malloc|mutex|lock_guard|vector<|Fft|fopen|printf`
# -- "a grep, not a proof", the same honest framing check_callback_shape.cmake
# already uses for the coherence guard it was modelled on. There is no
# allocation-counting test harness anywhere in this repo (memory
# a-fixed-defect-returns-through-the-silent-fallback.md's sibling note,
# OUT-R3): this guard plus a documented review IS the no-alloc proof for
# OutputEngine::render / AudioIo::audioDeviceIOCallbackWithContext, named so
# nobody goes looking for a harness that does not exist.
#
# Parameterised by SOURCE (the file) and FUNCTION (the unqualified function
# name to search for, e.g. "render" or "audioDeviceIOCallbackWithContext") --
# unlike check_callback_shape.cmake, which is hardcoded to one function in
# one file, because this script runs over TWO different functions in TWO
# different files (render() is in the OFF tree; the callback is ON-only).
#
# Mechanism reused verbatim from check_callback_shape.cmake: brace-counting
# over file(STRINGS)'s per-line list (never re-joined into a second CMake
# list -- a trailing `;` mid-line would silently re-split it), find the LAST
# non-comment line naming FUNCTION (the definition, not a declaration), walk
# forward tracking brace depth to find the body, then scan every line in the
# body for a forbidden token. ENCODING UTF-8 matters here too: this
# codebase's comments use "sec." not literal section-sign glyphs in THIS
# file on purpose, but the sources being scanned still carry them elsewhere,
# and file(STRINGS) without ENCODING UTF-8 silently mis-splits a multi-byte
# line.
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_no_rt_hazards: SOURCE not set")
endif()
if(NOT DEFINED FUNCTION)
    message(FATAL_ERROR "check_no_rt_hazards: FUNCTION not set")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "check_no_rt_hazards: no such file: ${SOURCE}")
endif()

file(STRINGS "${SOURCE}" lines ENCODING UTF-8)
list(LENGTH lines total_lines)
if(total_lines EQUAL 0)
    message(FATAL_ERROR "check_no_rt_hazards: ${SOURCE} is empty")
endif()

# Last non-comment line naming FUNCTION -- the definition. Matched as
# "FUNCTION(" (name immediately followed by an open paren), NOT a bare
# substring: OutputEngine.cpp also defines renderedSamples(), and a bare
# "render" search would latch onto THAT line instead (it contains "render"
# as a literal prefix) and misdirect the whole brace-walk below.
set(sig_pattern "${FUNCTION}\\(")
set(sig_index -1)
math(EXPR last_line "${total_lines} - 1")
foreach(i RANGE 0 ${last_line})
    list(GET lines ${i} line)
    if(line MATCHES "${sig_pattern}" AND NOT line MATCHES "^[ \t]*//")
        set(sig_index ${i})
    endif()
endforeach()
if(sig_index EQUAL -1)
    message(FATAL_ERROR "check_no_rt_hazards: ${FUNCTION} not found in ${SOURCE}")
endif()

# Walk forward from the signature, tracking brace depth, to find the body.
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
        "check_no_rt_hazards: could not find the closing brace of "
        "${FUNCTION}'s body in ${SOURCE} (unbalanced braces?)")
endif()

# Scan every body line (start+1 .. end-1, i.e. strictly inside the braces)
# for a forbidden token. A `//` comment line is still scanned deliberately --
# a hazard commented out with intent to re-enable is still a hazard waiting
# to happen, and this is a grep, not a semantic parser.
set(forbidden "new |malloc|mutex|lock_guard|vector<|Fft|fopen|printf")
set(violation_line -1)
set(violation_text "")
math(EXPR first_body_line "${body_start} + 1")
math(EXPR last_body_line "${body_end} - 1")
if(first_body_line LESS_EQUAL last_body_line)
    foreach(j RANGE ${first_body_line} ${last_body_line})
        list(GET lines ${j} candidate)
        if(candidate MATCHES "${forbidden}")
            set(violation_line ${j})
            set(violation_text "${candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT violation_line EQUAL -1)
    message(FATAL_ERROR
        "check_no_rt_hazards: forbidden token in ${FUNCTION}'s body "
        "(${SOURCE}, line ${violation_line}): ${violation_text}")
endif()

message(STATUS "${FUNCTION}_has_no_rt_hazards: OK (scanned lines ${first_body_line}-${last_body_line} of ${SOURCE})")
