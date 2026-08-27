# SPDX-License-Identifier: AGPL-3.0-or-later
#
# rta_platform -- the JUCE-facing half of platform/: AudioIo, which owns the
# juce::AudioDeviceManager and drives rta_platform_types::CaptureBus from the
# real audio callback. This is the ONE target in the repo allowed to link
# juce_audio_devices; everything the decision record calls dangerous (bounds
# checks, drop counting, ring draining) lives in rta_platform_types instead,
# so it is provable with no sound card (see platform/types/CMakeLists.txt).
#
# Built only when RTA_BUILD_APP is ON -- see root CMakeLists.txt and
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1, §3.3.

add_library(rta_platform STATIC
    src/AudioIo.cpp
    src/AudioIo_Devices.cpp
)

add_library(rta::platform ALIAS rta_platform)

target_include_directories(rta_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(rta_platform PUBLIC cxx_std_20)

target_link_libraries(rta_platform PUBLIC
    rta::platform_types
    rta::core
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ASIO wiring mirrored from app/CMakeLists.txt: AudioIo opens a device type by
# name (including "ASIO"), so its translation units need the same JUCE_ASIO
# define and include path the app target uses -- both are driven by the same
# WIN32/RTA_ENABLE_ASIO condition, so they always agree even though this
# directory configures before app/ does and RTA_ASIO_SDK_PATH is declared
# there (a cache variable set via -D is visible everywhere from the start of
# configure; only a same-run default from app/CMakeLists.txt would arrive
# too late, and that only matters if RTA_ENABLE_ASIO is ON without the SDK
# path also given on the command line -- app/CMakeLists.txt's FATAL_ERROR
# catches that misconfiguration before a build is attempted).
target_compile_definitions(rta_platform PRIVATE
    $<$<AND:$<BOOL:${WIN32}>,$<BOOL:${RTA_ENABLE_ASIO}>>:JUCE_ASIO=1>
)

if(WIN32 AND RTA_ENABLE_ASIO)
    target_include_directories(rta_platform PRIVATE "${RTA_ASIO_SDK_PATH}/common")
endif()

set_target_properties(rta_platform PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
