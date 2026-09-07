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
    src/dsp/AverageCount.cpp
    src/dsp/BandWeights.cpp
    src/dsp/BiquadResponse.cpp
    src/dsp/ButterworthDesign.cpp
    src/dsp/DelayFinder.cpp
    src/dsp/DualFftEngine.cpp
    src/dsp/Fft.cpp
    src/dsp/FilterBank.cpp
    src/dsp/FirDesign.cpp
    src/dsp/GroupDelay.cpp
    src/dsp/MinimumPhase.cpp
    src/dsp/MtwEngine.cpp
    src/dsp/MtwLayout.cpp
    src/dsp/OctaveBands.cpp
    src/dsp/OverloadDetector.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpatialAverage.cpp
    src/dsp/SpatialMtw.cpp
    src/dsp/SpectrumEngine.cpp
    src/dsp/TransferEstimator.cpp
    src/dsp/Weighting.cpp
    src/dsp/Window.cpp
    src/eq/BiquadDesign.cpp
    src/gen/Mls.cpp
    src/gen/Noise.cpp
    src/gen/Oscillator.cpp
    src/gen/Sweep.cpp
    src/gen/Synthetic.cpp
    src/ir/Deconvolver.cpp
    src/ir/Decay.cpp
    src/ir/DecayEnsemble.cpp
    src/ir/DecayLundeby.cpp
    src/ir/IrSpectrum.cpp
    src/ir/Polarity.cpp
    src/meter/Detector.cpp
    src/meter/Leq.cpp
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
