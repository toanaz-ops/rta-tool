# SPDX-License-Identifier: AGPL-3.0-or-later
#
# app/tests_juce/CMakeLists.txt's own 400-line-cap split (PR #43 fix round):
# the rtatool_main_component_tests target, pulled out whole. include()'d
# from that file at the same point it used to sit, with CMAKE_CURRENT_SOURCE_DIR
# unchanged (include() does not enter a new directory scope the way
# add_subdirectory() does), so every relative path below still resolves
# exactly as it did inline.

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
    # ALIGN-R8's XOVER coverage (PR #45), split across fix rounds (400-line
    # cap): round 1 in the first file, round 2's MEDIUM A/B + gap D in the
    # second, round 3's TraceLibrary::clear() survival in the third.
    test_main_component_panes_xover.cpp
    test_main_component_panes_xover_round2.cpp
    test_main_component_panes_xover_round3.cpp
    # Fix round MEDIUM F1: item 4's rail-scroll fix had no test.
    test_main_component_rail_layout.cpp
    # test_main_component_session.cpp: SAVE/OPEN SESSION against a real
    # MainComponent. test_main_component_session_fixround.cpp is its own
    # 400-line-cap split (PR #43 fix round). test_main_component_session_
    # path_guard.cpp is the fix round 2 MEDIUM R2-2 broad structural guard --
    # a separate file rather than growing fixround.cpp past its own cap.
    test_main_component_session.cpp
    test_main_component_session_fixround.cpp
    test_main_component_session_path_guard.cpp
    ${RTA_MAIN_COMPONENT_TEST_SOURCES}
)

target_include_directories(rtatool_main_component_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    # ../tests: reuses app/tests/CodeLines.h, like rtatool_routing_live_tests.
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests
    # app/dev-support: SyntheticSnapshot.h/.cpp's home, pulled in transitively
    # through RTATOOL_SNAPSHOT_SOURCES above (SyntheticSnapshot.cpp's own
    # self-include is "dev-support/SyntheticSnapshot.h").
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

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
