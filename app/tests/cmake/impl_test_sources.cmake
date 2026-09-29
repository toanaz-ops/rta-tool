# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/tests/CMakeLists.txt -- pure
# relocation. The remaining production .cpp files this target compiles
# directly (Analyser/AverageGroup/trace/export/api sources, plus the
# dev-support fixture generator) that
# do not fall under the base/api/spl test surfaces above -- linked straight
# into rtatool_analysis_tests, the same JUCE-free target every file above is
# proven in.
set(RTA_IMPL_TEST_SOURCES
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
    # DelayLocator.cpp was deleted (superseded by MainComponentDelay.cpp,
    # which polls OutputEngine telemetry through AnalysisThread instead --
    # see that file's own header comment). RawCaptureBuffer.h is still
    # needed: AnalysisThread::armLocateCapture owns one directly, header-only,
    # no .cpp.
    # SyntheticSnapshot.cpp lives in app/dev-support/ (moved out of
    # app/src/measure: never part of the shipped app, only this test target
    # and the offline snapshot tool) -- test_synthetic_snapshot.cpp and
    # several SPL/API tests call straight into it via ApiFixture.h.
    ${CMAKE_CURRENT_SOURCE_DIR}/../dev-support/SyntheticSnapshot.cpp
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
    # EqVerifyRunner.cpp: L7-EQ UI wave B tasks T7/T8 -- test_eq_verify_runner*.cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerifyRunner.cpp
    # EqPaneModel.cpp / EqTraceGrid.cpp / EqFirDesign.cpp: L7-EQ UI wave A tasks T2/T3 (plan
    # docs/plans/2026-09-29-eq-ui-lane-plan.md), JUCE-free, linked by
    # test_eq_pane_model.cpp / test_eq_fir_design.cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqPaneModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqTraceGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqFirDesign.cpp
    # CrossoverTopology.cpp: lane L7-ALIGN task D (record Sec.3) --
    # test_crossover_topology.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.cpp
    # VirtualTrace.cpp: lane L7-ALIGN task G (record Sec.5) --
    # test_virtual_trace.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.cpp
    # CaptureConverter.cpp: station-3 STORE plan, tasks T2/T3/T4 -- JUCE-free
    # (measure_has_no_framework_deps globs it explicitly, app/tests/
    # CMakeLists.txt), so test_capture_converter.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/CaptureConverter.cpp
    # AlignmentWizard.cpp + AlignmentWizardSignals.cpp: lane L7-ALIGN task H
    # (record Sec.2, Sec.7). The second is the first's polarity-signal half,
    # split at the 300-line aim -- the seam the record already draws between
    # the authoritative estimator and the table of witnesses.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizardSignals.cpp
    # CrossoverSurface.cpp: lane L7-ALIGN task I (record Sec.6) --
    # test_crossover_surface.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CrossoverSurface.cpp
    # ApiPolicy.cpp: lane L-API Tasks B and C (record sec.9, sec.15 R1) --
    # the Host/method/Bearer/cap policy and the rate limiter, framework-free
    # and server-library-free so record sec.11 items 6-9 are proven OFF.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiPolicy.cpp
    # ApiSerialise.cpp: lane L-API Task D (record sec.6) -- the fixed-axis
    # blocks. The spatial blocks are ApiSerialiseSpatial.cpp, and that
    # split is unconditional: discovering the 400-line cap mid-task is how
    # a file ends up at 399 lines doing two jobs.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialise.cpp
    # ApiSerialiseSpatial.cpp: lane L-API Task E (record sec.6, sec.15 R4)
    # -- /mtw, /average and /positions, the three blocks that carry a state
    # a well-meaning implementer drops.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialiseSpatial.cpp
    # ApiRoutes.cpp: the eight-endpoint TABLE, framework-free and
    # server-library-free -- split out of ApiServer.cpp at the seam the plan
    # named in advance ("validation versus routing"), so "which paths exist
    # and what each serialises" is asserted with no server in the picture.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiRoutes.cpp
    # ApiServer.cpp: lane L-API Task I (record sec.4, sec.8, sec.9, sec.10,
    # sec.15 R15). The ONE translation unit in this repository that includes
    # <httplib.h>, which no_server_library_outside_api enforces by name.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiServer.cpp
)
