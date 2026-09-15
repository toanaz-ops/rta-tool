# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The JUCE-free half of app/ -- measurement view models and plot geometry --
# compiled straight into a test target. It is NOT linked out of `rtatool`:
# that is a juce_add_gui_app and carries JUCE's module defines, and this
# target must stay buildable with RTA_BUILD_APP=OFF so CI exercises it.
# Registered OUTSIDE the RTA_BUILD_APP guard in the root CMakeLists.txt for
# exactly that reason -- see
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §4.2.
add_executable(rtatool_analysis_tests
    test_levels.cpp
    test_analyser.cpp
    test_analyser_transfer.cpp
    test_synthetic_snapshot.cpp
    test_plot_geometry.cpp
    test_readouts.cpp
    test_trace.cpp
    test_trace_decimator.cpp
    test_session_codec.cpp
    test_session_presets.cpp
    test_session_store.cpp
    test_trace_library.cpp
    test_repaint_gate.cpp
    test_phase_unwrap.cpp
    test_bode_layout.cpp
    test_phase_decimator.cpp
    test_coherence_alpha.cpp
    test_paired_drain.cpp
    test_synthetic_impairment.cpp
    test_workspace.cpp
    test_analyser_mtw.cpp
    test_column_map.cpp
    test_routing_plan.cpp
    test_average_group.cpp
    test_level_align.cpp
    test_capture_sequencer.cpp
    # test_output_policy.cpp: lane L7-OUT task D (record docs/dsp/
    # 2026-09-06-l7-output-path.md sec.3, sec.9) -- strict-solo/additive
    # policy over rta::platform::OutputEngine::routeOutput, JUCE-free (both
    # OutputEngine and CaptureSequencer already are), so it lives here rather
    # than in app/tests_juce.
    test_output_policy.cpp
    # test_fir_text.cpp: lane L7-FIR task F4's text export writer (record
    # docs/dsp/2026-09-06-l7-fir-export.md Sec.5) -- JUCE-free, so it lives
    # here beside test_session_codec.cpp rather than in app/tests_juce.
    test_fir_text.cpp
    # test_delay_locator.cpp: lane L7-DELAY task F1 (record docs/dsp/
    # 2026-09-06-l7-auto-delay.md sec.11) -- the raw-capture accumulator and
    # the Locate state machine over rta::platform::OutputEngine, JUCE-free
    # like OutputPolicy/CaptureSequencer, so it lives here.
    test_delay_locator.cpp
    # test_analysis_publish.cpp: task F2's wiring of AverageGroup into
    # AnalysisThread's publish path (record §6) -- the pure, JUCE-free half
    # of it (syncAverageGroupMembership/publishAverageGroup), exercised with
    # real Analysers and no bus/thread in the path.
    test_analysis_publish.cpp
    # test_eq_session.cpp: lane L7-EQ task E (record docs/dsp/
    # 2026-09-06-l7-auto-eq.md sec.2, sec.7) -- the app session model, the
    # coherence trust mask and the FilterSpec text export, all JUCE-free like
    # OutputPolicy/DelayLocator, so they live here rather than in
    # app/tests_juce.
    test_eq_session.cpp
    # test_eq_trust_export.cpp: task E's other two subjects -- the coherence
    # trust mask (record sec.4.1) and the FilterSpec text format (sec.7) --
    # split out of test_eq_session.cpp when that file grew past one job.
    test_eq_trust_export.cpp
    # test_eq_verify.cpp: lane L7-EQ task F (record sec.8) -- VERIFY over a
    # real rta::platform::OutputEngine with no device, same shape as
    # test_delay_locator.cpp.
    test_eq_verify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
    # AnalyserPublish.cpp: Analyser.cpp's publish()-site block-builder split
    # (task B0, docs/plans/2026-09-06-L6b-impl-plan.md) -- Analyser.cpp calls
    # into it now.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp
    # AverageGroup.cpp: task B3's spatial-average group (record §6).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.cpp
    # AnalysisPublish.cpp: task F2's wiring of AverageGroup into
    # AnalysisThread's publish path (record §6) -- test_analysis_publish.cpp
    # calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.cpp
    # CaptureSequencer.cpp: task B5's capture state machine (record §8).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.cpp
    # DelayLocator.cpp: lane L7-DELAY task F1's Locate sequence (record
    # docs/dsp/2026-09-06-l7-auto-delay.md sec.11.2) -- test_delay_locator.cpp
    # calls straight into it. RawCaptureBuffer.h is header-only, no .cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.cpp
    # SessionDecode.cpp: SessionCodec.cpp's decodeIndex split (task B0) --
    # SessionCodec.h declares decodeIndex, so any caller needs this linked
    # alongside SessionCodec.cpp's encodeIndex.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionDecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceBlobCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.cpp
    # FirTextWriter.cpp: task F4's text export writer (record Sec.5) --
    # test_fir_text.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirTextWriter.cpp
    # EqSession.cpp: lane L7-EQ task E (record Sec.2, Sec.7) --
    # test_eq_session.cpp calls straight into it. EqTrustMask.h and
    # EqTextExport.h are header-only, no .cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.cpp
    # EqVerify.cpp: lane L7-EQ task F (record Sec.8) -- test_eq_verify.cpp
    # calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.cpp
)
target_include_directories(rtatool_analysis_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(rtatool_analysis_tests PRIVATE
    # rta::platform_types: RoutingPlan.h (task B2) is a pure function over
    # rta::platform::ChannelConfig, JUCE-free like the rest of this target --
    # ChannelConfig.h lives in that target, not rta::core.
    rta::core rta::platform_types Catch2::Catch2WithMain)

# No `include(Catch)` here -- same reasoning as platform/tests/CMakeLists.txt:
# core/tests/CMakeLists.txt loads that CMake module once for the whole
# configure run (function/macro definitions are global, not directory-scoped),
# and root CMakeLists.txt keeps core/tests ordered before this directory for
# exactly that reason.
catch_discover_tests(rtatool_analysis_tests)

# --- Architectural guard ---------------------------------------------------
# The measurement view model (app/src/measure/{Levels,Snapshot,SnapshotSource,
# Analyser,SyntheticSnapshot}) and the plot's pure math (PlotGeometry.h) must
# stay as JUCE-free as rta_core -- that is what makes them compile, and this
# whole test target link, with RTA_BUILD_APP=OFF (plan §1.3).
#
# This reuses core's guard script via its GLOBS escape hatch rather than its
# CORE_DIR convention: app/src/measure has no include/src/tests split, and --
# unlike rta_core / rta_platform_types -- it is not entirely JUCE-free by
# design. AnalysisThread.{h,cpp} and SyntheticInput.{h,cpp} land in a later
# wave and use juce::Thread on purpose (plan §3.4's "JUCE?" column says so
# explicitly), so the file list below is named explicitly instead of
# glob-scanning the whole directory -- whoever adds those two files must NOT
# add them here.
add_test(NAME measure_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            "-DGLOBS=${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Levels.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Snapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SnapshotSource.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PlotGeometry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/Readouts.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Trace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TraceDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceBlobCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/RepaintGate.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/AxisMetrics.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/BodeLayout.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PhaseDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CoherenceAlpha.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PairedDrain.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticImpairment.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Workspace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PaneRegistry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/ColumnMap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodecDetail.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionDecode.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RoutingPlan.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/LevelAlign.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/OutputPolicy.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirTextWriter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RawCaptureBuffer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqTrustMask.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/EqTextExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.cpp"
            -DLABEL=app_measure
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_framework_deps.cmake)
