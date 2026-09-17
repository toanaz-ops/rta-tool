# SPDX-License-Identifier: AGPL-3.0-or-later
include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(rta_core_tests
    test_average_count.cpp
    test_delay_finder.cpp
    test_delay_policy.cpp
    test_delay_policy_trust.cpp
    test_residual_tracker.cpp
    test_dualfft.cpp
    test_transfer_estimator.cpp
    test_transfer_golden.cpp
    test_mtw_layout.cpp
    test_mtw_engine.cpp
    test_mtw_response.cpp
    test_mtw_golden.cpp
    test_spatial_average.cpp
    test_spatial_mtw.cpp
    test_spatial_mtw_absence.cpp
    test_overload.cpp
    test_spatial_golden.cpp
    test_group_delay.cpp
    test_window.cpp
    test_ringbuffer.cpp
    test_fft.cpp
    test_bands.cpp
    test_spectrum.cpp
    test_bandweights.cpp
    test_synthetic.cpp
    test_biquad.cpp
    test_biquad_response.cpp
    test_minimum_phase.cpp
    test_fir_design.cpp
    test_fir_design_minphase.cpp
    test_fir_golden.cpp
    test_biquad_design.cpp
    test_virtual_processor.cpp
    test_virtual_processor_biquad.cpp
    test_crossover_fit.cpp
    test_crossover_band_fit.cpp
    test_crossover_band_candidates.cpp
    test_excess_phase.cpp
    test_dip_classifier.cpp
    test_eq_allocator.cpp
    test_eq_allocator_placement.cpp
    test_butterworth.cpp
    test_align_order4_identity.cpp
    test_filterbank.cpp
    test_weighting.cpp
    test_detector.cpp
    test_leq.cpp
    test_generator_prng.cpp
    test_generator_osc.cpp
    test_generator_noise.cpp
    test_generator_sweep.cpp
    test_generator_mls.cpp
    test_ir_deconvolver.cpp
    test_ir_golden.cpp
    test_ir_decay.cpp
    test_ir_polarity.cpp
    test_relative_polarity.cpp
    test_relative_polarity_guard.cpp
    test_ir_spectrum.cpp
    # test_block.cpp: lane L6a task W0-A (record docs/dsp/2026-09-16-spl-pro-
    # l6a.md §2, §3, §8) -- the SPL block, its sample-count clock, and the
    # energy sum every longer window is recomputed from.
    test_block.cpp
)

# Golden vectors are committed, so the C++ suite needs neither Python nor
# NumPy. Regenerate them with tools/gen_golden.py and review the diff.
target_compile_definitions(rta_core_tests PRIVATE
    RTA_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    # RTA_REPO_ROOT: L7-ALIGN task E6 scans a header from the test itself, so
    # "relativePolarity ships no verdict" runs in CI on all three operating
    # systems instead of living in a grep somebody has to remember to type.
    RTA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_include_directories(rta_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rta_core_tests PRIVATE rta::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(rta_core_tests)

# --- Architectural guard -------------------------------------------------
# rta_core must stay free of GUI / audio-device frameworks. This is a real
# test, not a comment: it fails the build pipeline if anyone adds such an
# include to core/.
add_test(NAME core_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_framework_deps.cmake
)

# --- Representation guard ------------------------------------------------
# The ba (transfer-function polynomial) form of these filters is numerically
# invalid at the bottom of the band table -- its denominator roots leave the
# unit circle. This fails the pipeline if anyone reintroduces it, in core or in
# the generator scripts. See docs/dsp/2026-08-27-filterbank.md.
add_test(NAME filter_design_has_no_polynomial_form
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -DTOOLS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../tools
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_polynomial_form.cmake
)

# --- Honesty guard ---------------------------------------------------------
# core/ must never claim IEC 61672-1 "Class 1" conformance -- the tolerance
# table is not fully sourced. See docs/dsp/2026-08-27-weighting-and-meters.md
# and core/tests/check_no_conformance_claim.cmake.
add_test(NAME core_makes_no_class_1_claim
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_conformance_claim.cmake
)

# --- Coherence-gate guard -------------------------------------------------
# The coherence gate (docs/dsp/2026-08-28-dual-fft.md §3) is only real if
# something enforces it: TransferEstimator.cpp's makeSnapshot() must stay the
# ONE place TransferSnapshot::coherence is assigned. See
# core/tests/check_coherence_gate.cmake.
add_test(NAME coherence_gate_is_not_bypassed
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_coherence_gate.cmake
)

# --- Test-name portability guard -------------------------------------------
# Every Catch2 test name in the repo must be pure ASCII: ctest runs a
# discovered test by handing its name back to the binary as a filter, and that
# round trip only survives non-ASCII where the whole toolchain agrees on the
# encoding. One U+00A7 in one name turned windows-latest red for a test that
# never executed an assertion (CI run 34043346767). See
# core/tests/check_test_names_are_ascii.cmake.
#
# Scans EVERY test directory, including the ones only compiled with
# RTA_BUILD_APP=ON -- a name is wrong in the source whether or not this
# configuration builds it, and a textual scan is cheap enough to run always.
# --- atomic<shared_ptr> portability guard ----------------------------------
# `std::atomic<std::shared_ptr<T>>` is valid C++20 that MSVC and libstdc++
# compile happily and Apple's libc++ refuses outright -- so the mistake is
# invisible on every machine in this project and red only on macos CI. Only
# app/src/measure/AtomicSharedPtr.h may name it; everything else uses
# rta::measure::AtomicSharedPtr. See core/tests/check_no_std_atomic_shared_ptr.cmake.
add_test(NAME no_std_atomic_over_shared_ptr
    COMMAND ${CMAKE_COMMAND}
            "-DDIRS=${CMAKE_SOURCE_DIR}/core;${CMAKE_SOURCE_DIR}/app;${CMAKE_SOURCE_DIR}/platform;${CMAKE_SOURCE_DIR}/ui;${CMAKE_SOURCE_DIR}/tools"
            -DALLOW=${CMAKE_SOURCE_DIR}/app/src/measure/AtomicSharedPtr.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_std_atomic_shared_ptr.cmake
)

add_test(NAME test_names_are_ascii
    COMMAND ${CMAKE_COMMAND}
            "-DDIRS=${CMAKE_SOURCE_DIR}/core/tests;${CMAKE_SOURCE_DIR}/app/tests;${CMAKE_SOURCE_DIR}/app/tests_juce;${CMAKE_SOURCE_DIR}/platform/tests;${CMAKE_SOURCE_DIR}/platform/tests_juce;${CMAKE_SOURCE_DIR}/ui/tests"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_test_names_are_ascii.cmake
)
