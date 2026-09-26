# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The JUCE half of app/src/view -- currently StoredTraceLayer, which owns a
# juce::Image and therefore cannot be compiled into rtatool_analysis_tests.
#
# This directory exists for the same reason platform/tests_juce does, and is
# registered the same way: app/tests is added UNCONDITIONALLY by the root
# CMakeLists.txt (outside the RTA_BUILD_APP guard) so the JUCE-free half of
# app/ still builds and runs under CI's RTA_BUILD_APP=OFF configuration. The
# targets THIS file links -- az_ui and the juce:: modules -- only exist inside
# root CMakeLists.txt's `if(RTA_BUILD_APP)` block, so this directory is added
# there and only there. That is what stops the JUCE-free configure from trying
# to resolve targets that plainly do not exist in that tree.
#
# It is deliberately NOT folded into platform/tests_juce: that target links
# rta::platform and pulls in no GUI module and no app include root. Putting
# view tests there would make the audio-device test target depend on the app's
# drawing code, which is a dependency in the wrong direction.
#
# No window, no desktop peer and no message loop is created by anything here.
# StoredTraceLayer rasterises into a juce::Image through juce::Graphics, which
# is the software renderer -- tools/snapshot.cpp already proves that path runs
# headlessly (see its header comment). The tests construct their own
# juce::Image and read pixels back with juce::Image::BitmapData.

add_executable(rtatool_view_tests
    test_stored_trace_layer.cpp
    test_trace_stroke.cpp
    test_transfer_view.cpp
    # Split out of test_transfer_view.cpp (task 10's final-review fix), the
    # same way test_analyser_transfer.cpp split off test_analyser.cpp: that
    # file was at the project's 400-line hard cap, so the unwrap-toggle case
    # -- self-contained, needing no fixture the helpers header does not
    # already provide -- moved here to make room for the new margin-overpaint
    # case.
    test_transfer_view_unwrap.cpp
    test_transfer_view_mtw.cpp
    # test_transfer_source_toggle.cpp: the per-plot MTW/Fixed UI control
    # (docs/reports/005-mtw-engine.md "Known gaps").
    test_transfer_source_toggle.cpp
    test_workspace_view.cpp
    # test_main_component_pane_factory.cpp: fix round, PR #26 -- calls the
    # REAL makePaneFactory (PaneFactory.h) and checks its Spl branch builds
    # an SplView, not an RtaView.
    test_main_component_pane_factory.cpp
    # test_routing_matrix.cpp: task B7's routing matrix (record §6, §7) --
    # composes az_ui's GridPanel, JUCE-using for the same reason
    # TransferSourceToggle.cpp already gives.
    test_routing_matrix.cpp
    # StoredTraceLayer.cpp's own include list is the whole dependency set:
    #   view/StoredTraceLayer.h -> juce_gui_basics + view/PlotGeometry.h (header only)
    #   trace/TraceLibrary.h    -> needs TraceLibrary.cpp linked (entries/revision/setVisible)
    #   view/MeasureColours.h   -> az_ui (aliases only, but the module must link)
    #   view/TraceDecimator.h   -> header only, all inline
    #   view/TraceStroke.h      -> needs TraceStroke.cpp linked (the stroking half, task 7)
    #   view/PhaseDecimator.h,
    #   view/CoherenceAlpha.h   -> header only, all inline
    #   view/PlotAxes.h         -> needs PlotAxes.cpp linked (drawGrid /
    #                              drawFrequencyLabels / drawLevelLabels,
    #                              needed once test_transfer_view.cpp calls
    #                              through TransferView.cpp)
    # Nothing else. SessionCodec.cpp / TraceBlobCodec.cpp are NOT listed even
    # though TraceLibrary.h includes SessionCodec.h: that include supplies the
    # LibraryEntry STRUCT, and no function defined in those translation units
    # is odr-used from here. Adding them would compile session serialisation
    # into a view test for no reason and hide a real link dependency behind a
    # source list that is wider than the code.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/StoredTraceLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TraceStroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.cpp
    # test_transfer_view.cpp's own additional dependency set (task 8):
    #   view/TransferView.h/.cpp    -> the composite under test
    #   view/TransferRibbon.h/.cpp  -> its ribbon half (file-length split)
    #   measure/PhaseUnwrap.h/.cpp  -> the unwrap toggle (decision 4)
    #   measure/SyntheticSnapshot.h/.cpp, measure/Analyser.h/.cpp ->
    #     makeSyntheticTransfer's own dependency chain (SyntheticSnapshot.h
    #     includes Analyser.h for makeSyntheticSnapshot; Analyser.cpp must
    #     link even though this target never calls it, for the same
    #     one-translation-unit reason app/CMakeLists.txt names it explicitly
    #     in both GUI binaries).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TransferView.cpp
    # TransferViewDraw.cpp: TransferView.cpp's own drawing-helper split
    # (task B0) -- TransferView.cpp calls into it now.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TransferViewDraw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TransferRibbon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PlotAxes.cpp
    # test_transfer_view_mtw.cpp's own additional dependency (task 7):
    #   view/MtwLayer.h/.cpp -> the MTW-specific per-band alpha and seam
    #     drawing, JUCE-using so it lives here rather than in ColumnMap.h.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/MtwLayer.cpp
    # view/MtwReadout.h/.cpp -> the per-band integration-seconds strip
    # (record §5, station-4 fix pass finding 1), same JUCE-using reason.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/MtwReadout.cpp
    # test_transfer_source_toggle.cpp's own additional dependency:
    #   view/TransferSourceToggle.h/.cpp -> the per-plot MTW/Fixed control
    #     under test, JUCE-using (juce::TextButton) for the same reason
    #     MtwLayer.cpp/MtwReadout.cpp already give.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TransferSourceToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
    # AnalyserPublish.cpp: Analyser.cpp's publish()-site block-builder split
    # (task B0) -- Analyser.cpp calls into it now.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp
    # test_workspace_view.cpp's own additional dependency set (task 10):
    #   view/WorkspaceView.h/.cpp -> the pane container under test; needs
    #     az::ui::splitVertically, already linked in through az_ui below.
    #   view/RtaView.h/.cpp       -> one of the two concrete pane types the
    #     test builds through the factory and dynamic_casts back to, to read
    #     library()/storedLayer() -- not needed by any OTHER file in this
    #     target, hence not listed until now.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/WorkspaceView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/RtaView.cpp
    # RoutingMatrix.cpp: test_routing_matrix.cpp's own dependency (task B7).
    # Links rta::platform_types (below) for ChannelConfig.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/RoutingMatrix.cpp
    # test_main_component_pane_factory.cpp's own dependency (fix round, PR
    # #26): PaneFactory.cpp is the REAL production closure MainComponent.cpp
    # builds workspace_ with, split into its own translation unit precisely
    # so it links here with no AnalysisThread/ApiServer/DevicePanel needed --
    # only the pane types this target already links (RtaView.cpp above,
    # TransferView.cpp already linked for test_transfer_view.cpp) plus
    # SplView.cpp, new here.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PaneFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/SplView.cpp
)

# app/src as an include root, exactly as rtatool and rtatool_analysis_tests do:
# these sources include each other as "view/Foo.h" / "trace/Foo.h", never as a
# path relative to the including file.
target_include_directories(rtatool_view_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# az_ui carries its own juce_gui_basics dependency (see ui/az_ui/az_ui.h's
# module declaration), but juce_gui_basics is named explicitly anyway: this
# target uses juce::Image and juce::Graphics DIRECTLY, and a target should
# state the modules its own code includes rather than inherit them from a
# neighbour that happens to need the same ones today.
target_link_libraries(rtatool_view_tests PRIVATE
    az_ui
    # rta::core: Analyser.cpp (pulled in for makeSyntheticTransfer's own
    # dependency chain, see the target_sources comment above) links against
    # rta_core's DSP engines.
    rta::core
    # rta::platform_types: RoutingMatrix.cpp/test_routing_matrix.cpp's own
    # ChannelConfig dependency (task B7).
    rta::platform_types
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

# JUCE_WEB_BROWSER / JUCE_USE_CURL mirror rtatool's own settings so this target
# compiles the same juce_gui_basics as the app does -- a module compiled with
# different flags here would be testing a different build of the framework.
target_compile_definitions(rtatool_view_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

# No `include(Catch)` here, for the same reason app/tests/CMakeLists.txt and
# platform/tests_juce/CMakeLists.txt both give: that CMake module -- and the
# CMAKE_MODULE_PATH entry it needs -- is loaded once by core/tests/
# CMakeLists.txt, and CMake function/macro definitions (unlike variables) are
# global for the rest of the configure run rather than directory-scoped. Root
# CMakeLists.txt orders add_subdirectory(core/tests) before the RTA_BUILD_APP
# block for exactly this reason.
#
# TEST_PREFIX puts "view/" in every ctest case name so `ctest -R view/` selects
# this whole target, matching the convention platform/tests_juce established
# (catch_discover_tests names each ctest case after the TEST_CASE name, not its
# tags, so a tag is not selectable from ctest at all).
catch_discover_tests(rtatool_view_tests TEST_PREFIX "view/")

# --- Routing, live (task B2) ------------------------------------------------
# AnalysisThread is JUCE-owning (juce::Thread) but carries NO GUI: this
# target links juce_core only, never juce_gui_basics/az_ui, which is why it
# is a separate executable from rtatool_view_tests above rather than folded
# into it. Proves record §11 T11 (two transfer functions sharing a reference
# get equal hop counters; two naming different references get independent
# ones) against a REAL CaptureBus + AnalysisThread + poll loop -- the pure
# routing DECISION itself is already covered OFF-build by
# app/tests/test_routing_plan.cpp.
add_executable(rtatool_routing_live_tests
    test_routing_live.cpp
    # test_delay_locate.cpp: lane L7-DELAY task F2 (record docs/dsp/
    # 2026-09-06-l7-auto-delay.md sec.11.2, sec.1.6, sec.8) -- the raw-
    # capture accumulator wired into drainPaired, and Apply's rebuild --
    # against the SAME real AnalysisThread/CaptureBus shape
    # test_routing_live.cpp uses, so it lives in this target too.
    test_delay_locate.cpp
    # test_spl_drain.cpp: lane L6a task W0-D (research sec.C4, SPL-R1) -- the
    # SPL feed above the kMaxTransferFunctions cap, the unrouted drain, and
    # the Gap the paired drain cannot avoid. Against the same real
    # AnalysisThread/CaptureBus shape test_routing_live.cpp uses.
    test_spl_drain.cpp
    # test_spl_log_wiring.cpp: lane L6a task W2-E2a (plan amendment "W2-E --
    # the wiring nobody was assigned") -- AnalysisThread::enableSplLogging
    # actually writing a log to disk, against the same real AnalysisThread/
    # CaptureBus shape test_spl_drain.cpp uses. The queue/writer mechanics
    # themselves are proven OFF in app/tests/test_spl_log_pipeline.cpp; what
    # only a live thread can prove is that AnalysisThread wires the two
    # together correctly.
    test_spl_log_wiring.cpp
    # test_spl_log_wiring_disable.cpp: LOW follow-up batch, item 9 -- split
    # out of test_spl_log_wiring.cpp (over the 400-line hard cap) along its
    # own seam: disableSplLogging() actually drains and joins the writer
    # thread rather than a no-op a background writer's own timing could mask.
    test_spl_log_wiring_disable.cpp
    # test_spl_epoch_freeze.cpp: station-4 fix round, PR #31, round 3,
    # verifier finding 1 (MEDIUM, the epoch race) -- a real AnalysisThread
    # over a real CaptureBus, proving feedSpl() freezes the instant
    # CaptureBus::prepare() bumps the epoch, with no second
    # enableSplLogging() call to rescue it. Same shape as test_spl_drain.cpp.
    test_spl_epoch_freeze.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisThread.cpp
    # --- L6a Wave 0 task W0-D ---------------------------------------------
    # The SPL feed AnalysisThread.cpp now calls: its own split-off half, the
    # session that owns the meters, and the per-channel chain. D1b/D2/D3 in
    # test_spl_drain.cpp exercise all three through a REAL AnalysisThread.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisThreadSpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.cpp
    # --- end L6a Wave 0 ---------------------------------------------------
    # --- L6a Wave 2 task W2-E1 ---------------------------------------------
    # SplChannelState.cpp: AnalysisThreadSpl.cpp's feedSpl() and
    # AnalysisPublish.cpp's buildSplBlockView() (both linked below) now call
    # into it directly, so it has to be named here too or this target fails
    # to link with undefined symbols. SplHistory.cpp (W2-A) and SplAlarms.cpp
    # (W2-B) had NO caller anywhere in app/src before this task, so were
    # never in this list either -- SplChannelState.cpp is their first real
    # caller, and both have to be named here now for the same reason.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplAlarms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplChannelState.cpp
    # --- end L6a Wave 2 task W2-E1 ------------------------------------------
    # --- L6a task W2-E2a -----------------------------------------------------
    # AnalysisThread.h now includes export/SplLogPipeline.h and
    # AnalysisThreadSpl.cpp calls into it, so both halves of the log-writing
    # pipeline need linking here too. SplSessionHeader.h is header-only
    # (writeSessionHeaderFile is DEFINED in SplLogWriter.cpp, that pair's own
    # class comment).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogWriter.cpp
    # --- end L6a task W2-E2a --------------------------------------------------
    # DelayLocator.cpp is NOT needed here: this target's tests exercise
    # AnalysisThread's own armLocateCapture/applyReferenceDelay directly,
    # not the JUCE-free DelayLocator state machine (that lives entirely in
    # rtatool_analysis_tests, task F1).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RawCaptureBuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp
    # AverageGroup.cpp / AnalysisPublish.cpp: task F2 wires AnalysisThread's
    # publishIfDue() through both -- AnalysisThread.h now includes
    # measure/AverageGroup.h and measure/AnalysisPublish.h, so this target
    # needs their definitions linked even though this file's own tests
    # exercise routing, not the group publish.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.cpp
)

target_include_directories(rtatool_routing_live_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    # L6a W0-D: test_spl_drain.cpp's structural scans share app/tests's one
    # comment-stripping reader (CodeLines.h) rather than carrying a second
    # copy -- PR #9's verifier is why that file exists at all.
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests)

# RTA_REPO_ROOT: test_spl_drain.cpp's D1 and D4 scans read the real source
# files, so the "no second ring reader" and "platform/ untouched" rules run on
# every build instead of living in a grep somebody has to remember to type.
target_compile_definitions(rtatool_routing_live_tests PRIVATE
    RTA_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(rtatool_routing_live_tests PRIVATE
    rta::core
    rta::platform_types
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

target_compile_definitions(rtatool_routing_live_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

catch_discover_tests(rtatool_routing_live_tests TEST_PREFIX "routing_live/")

# --- FIR WAV export (lane L7-FIR, task F5) ----------------------------------
# The one JUCE-using half of the FIR export writers (FirWavWriter.cpp --
# WavAudioFormat); the JUCE-free text writer (FirTextWriter.cpp, task F4)
# lives in rtatool_analysis_tests instead. Links rta::core (FirDesign.h's
# FirResult) and juce::juce_audio_formats directly -- already transitively
# present via juce_audio_utils (record Sec.6, verified
# juce_audio_utils.h:54), named explicitly anyway per this file's own
# rtatool_view_tests convention of stating the modules a target's own code
# includes rather than inheriting them from a neighbour.
add_executable(rtatool_export_tests
    test_fir_wav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirWavWriter.cpp
    # FirTextWriter.cpp: JUCE-free (rtatool_analysis_tests' own copy, task
    # F4), linked here too because it defines firFilenameStem() -- test_fir_wav
    # T3's own target, and FirWavWriter.cpp only calls
    # computeNormalizationScale (header-only, FirExport.h).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirTextWriter.cpp
)

target_include_directories(rtatool_export_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(rtatool_export_tests PRIVATE
    rta::core
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

target_compile_definitions(rtatool_export_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

catch_discover_tests(rtatool_export_tests TEST_PREFIX "export/")

# --- Pane selector, against a REAL MainComponent (owner decision 2026-09-26)
# test_main_component_panes.cpp needs a real MainComponent -- not a hand-
# copied fixture -- to prove selectPaneView() rebuilds workspace_ through the
# real makePaneFactory AND never touches analysisThread_'s SPL logging. That
# means every dependency MainComponent.h names: DevicePanel, ApiServer
# (rta_httplib), CalibrationSession, the SPL log pipeline, RoutingMatrix,
# AudioIo (rta::platform) -- the same graph rtatool_snapshot already proves
# buildable offscreen for main-live.png (tools/snapshot.cpp's own header
# comment on why that is safe with no device and no message loop).
#
# Reusing RTATOOL_SNAPSHOT_SOURCES rather than a third hand-typed copy of the
# same ~40-file list: this is the SOURCE LIST (which .cpp files to compile),
# not a shared static library -- each target still gets its own objects,
# built with its own JUCE_MODULE_AVAILABLE_* defines (the reason app/
# CMakeLists.txt's own comment gives for not sharing a library between
# rtatool and rtatool_snapshot applies to a shared archive, not to reusing
# which files a THIRD target names). tools/snapshot.cpp itself is removed --
# this target supplies its own Catch2 main instead.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/rtatool_snapshot_sources.cmake)
set(RTA_MAIN_COMPONENT_TEST_SOURCES ${RTATOOL_SNAPSHOT_SOURCES})
list(REMOVE_ITEM RTA_MAIN_COMPONENT_TEST_SOURCES "${CMAKE_SOURCE_DIR}/tools/snapshot.cpp")
# RTATOOL_SNAPSHOT_SOURCES' remaining entries are relative to app/ (the
# directory app/CMakeLists.txt's own include() resolves them from) -- this
# CMakeLists.txt is a different directory, so re-root every entry onto an
# absolute, unambiguous path rather than relying on CMAKE_CURRENT_SOURCE_DIR.
list(TRANSFORM RTA_MAIN_COMPONENT_TEST_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/app/")

add_executable(rtatool_main_component_tests
    test_main_component_panes.cpp
    # Fix round MEDIUM F1: item 4's rail-scroll fix had no test.
    test_main_component_rail_layout.cpp
    ${RTA_MAIN_COMPONENT_TEST_SOURCES}
)

target_include_directories(rtatool_main_component_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(rtatool_main_component_tests PRIVATE
    RtaToolFonts
    az_ui
    rta::core
    rta::platform
    rta_httplib
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

target_compile_definitions(rtatool_main_component_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    # Fix round LOW F6: this target includes MainComponentTestAccess.h --
    # see that header's own compile-time guard.
    RTA_MAINCOMPONENT_TEST_ACCESS=1
)

catch_discover_tests(rtatool_main_component_tests TEST_PREFIX "main_component/")
