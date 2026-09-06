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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp
)

target_include_directories(rtatool_routing_live_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

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
