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
# Fails if any translation unit under CORE_DIR or TOOLS_DIR materialises a
# transfer-function (ba) polynomial anywhere in the filter-design chain. See
# docs/dsp/2026-08-27-filterbank.md: for the lowest third-octave band at 48 kHz
# that polynomial's denominator has a root outside the unit circle and its
# impulse response diverges to 7.4e+130 in double precision, while the
# identical filter as SOS stays bounded at 2.0e-06. The coefficient vector is
# not a less convenient representation of the same filter -- it is a wrong one.
if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR not set")
endif()
if(NOT DEFINED TOOLS_DIR)
    message(FATAL_ERROR "TOOLS_DIR not set")
endif()

file(GLOB_RECURSE sources
    "${CORE_DIR}/include/*.h"
    "${CORE_DIR}/src/*.cpp"
    "${CORE_DIR}/tests/*.cpp"
    "${TOOLS_DIR}/*.py"
)

# Patterns chosen not to match the prose that explains the prohibition (this
# file's own header, and the docstring in gen_filterbank.py): each requires the
# quoting or module-prefix syntax an actual call site uses, which a sentence
# about the practice does not.
set(patterns
    "output[ ]*=[ ]*['\"]ba['\"]"
    "zpk2tf"
    "tf2sos"
    "tf2zpk"
    "sos2tf"
    "np\\.poly\\("
    "numpy\\.poly\\("
    "signal\\.lfilter"
    "freqz\\(b"
)

set(violations "")
foreach(file IN LISTS sources)
    file(READ "${file}" content)
    foreach(pattern IN LISTS patterns)
        if(content MATCHES "${pattern}")
            list(APPEND violations "${file}: ${pattern}")
        endif()
    endforeach()
endforeach()

list(LENGTH sources n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "No sources found under ${CORE_DIR} / ${TOOLS_DIR} -- check is not actually running")
endif()

if(violations)
    message(FATAL_ERROR
        "A transfer-function (ba) polynomial pattern was found. This is never a "
        "convenience -- see docs/dsp/2026-08-27-filterbank.md. Offending files:\n"
        "  ${violations}")
endif()

message(STATUS "filter_design_has_no_polynomial_form: OK (${n_sources} files scanned)")
