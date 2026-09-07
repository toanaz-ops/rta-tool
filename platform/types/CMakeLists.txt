# SPDX-License-Identifier: AGPL-3.0-or-later
#
# rta_platform_types -- the JUCE-free half of platform/: channel roles, the
# lock-free capture bus, and plain device/fault structs.
#
# ARCHITECTURAL RULE: same as rta_core -- this target must NEVER link against
# JUCE, Qt, or any audio-device API. Enforced by the
# platform_types_has_no_framework_deps test in platform/tests, not by this
# comment. Built ALWAYS (see root CMakeLists.txt), even when RTA_BUILD_APP is
# OFF, so CI proves the bounds-checking and drop-counting logic with no sound
# card -- see docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1.

add_library(rta_platform_types STATIC
    src/CaptureBus.cpp
    # OutputEngine.cpp: lane L7-OUT's lock-free generator output path (record
    # docs/dsp/2026-09-06-l7-output-path.md sec.6) -- JUCE-free like the rest
    # of this target, so it is provable with no device (plan task B).
    src/OutputEngine.cpp
)

add_library(rta::platform_types ALIAS rta_platform_types)

target_include_directories(rta_platform_types
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(rta_platform_types PUBLIC cxx_std_20)

# Uses rta::dsp::RingBuffer.
target_link_libraries(rta_platform_types PUBLIC rta::core)

set_target_properties(rta_platform_types PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
