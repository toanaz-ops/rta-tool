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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TransferRibbon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PlotAxes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
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
