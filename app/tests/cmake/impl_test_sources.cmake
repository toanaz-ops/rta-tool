# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/tests/CMakeLists.txt -- pure
# relocation. The remaining production .cpp files this target compiles
# directly (Analyser/AverageGroup/DelayLocator/trace/export/api sources) that
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
    # CrossoverTopology.cpp: lane L7-ALIGN task D (record Sec.3) --
    # test_crossover_topology.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.cpp
    # VirtualTrace.cpp: lane L7-ALIGN task G (record Sec.5) --
    # test_virtual_trace.cpp calls straight into it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.cpp
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
