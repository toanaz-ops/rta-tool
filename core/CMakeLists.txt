# SPDX-License-Identifier: AGPL-3.0-or-later
#
# rta_core -- the measurement engine.
#
# ARCHITECTURAL RULE: this target must NEVER link against JUCE, Qt, or any
# audio-device API. It takes buffers of floats and returns numbers, so that
# every algorithm in it can be proven correct offline, on CI, with no sound
# card and no microphone. The rule is enforced by the `core_has_no_framework_deps`
# test in core/tests -- not by this comment.

add_library(rta_core STATIC
    src/dsp/BandWeights.cpp
    src/dsp/Fft.cpp
    src/dsp/OctaveBands.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpectrumEngine.cpp
    src/dsp/Window.cpp
    src/gen/Synthetic.cpp
)

add_library(rta::core ALIAS rta_core)

target_include_directories(rta_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(rta_core PUBLIC cxx_std_20)

set_target_properties(rta_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
