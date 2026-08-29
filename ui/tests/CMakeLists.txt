# SPDX-License-Identifier: AGPL-3.0-or-later
#
# az_ui's own tests. The module claims to be portable -- liftable into another
# JUCE project with one `juce_add_module` line -- and a portable module that can
# only be tested through this application's test targets is not portable, it is
# this application's GUI in a different folder with a promise attached.
#
# Registered inside root CMakeLists.txt's `if(RTA_BUILD_APP)` block: az_ui and
# the juce:: targets do not exist in the RTA_BUILD_APP=OFF configure at all.
#
# No `include(Catch)` here -- core/tests/CMakeLists.txt loads that module once
# for the whole configure run and root CMakeLists.txt orders it first. Same
# reasoning as app/tests_juce/CMakeLists.txt.
add_executable(az_ui_tests test_layout.cpp)

target_link_libraries(az_ui_tests PRIVATE
    az_ui
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

target_compile_definitions(az_ui_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

catch_discover_tests(az_ui_tests TEST_PREFIX "ui/")
