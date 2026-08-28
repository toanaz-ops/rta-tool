# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests for the JUCE half of platform/ -- rta_platform / AudioIo. This cannot
# live inside platform/tests/CMakeLists.txt: that directory is added
# UNCONDITIONALLY (root CMakeLists.txt, outside the RTA_BUILD_APP guard) so
# rta_platform_tests -- the JUCE-free half -- still builds and runs under
# CI's RTA_BUILD_APP=OFF configuration (see that file's own header comment
# and docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.1). rta::platform,
# the library these tests link, is only DEFINED inside root CMakeLists.txt's
# `if(RTA_BUILD_APP)` block, after `add_subdirectory(platform)`. Registering
# this directory there, and only there, is what stops the JUCE-free CI
# configure from ever trying to resolve a target that plainly does not exist
# in that tree.
#
# No juce::AudioIODevice is ever opened by anything under this directory.
# AudioIo publicly implements juce::AudioIODeviceCallback (see
# platform/include/rta/platform/AudioIo.h), so its non-realtime overrides
# (audioDeviceAboutToStart / audioDeviceStopped / audioDeviceError) and the
# hot callback itself are exercised by calling them directly; wherever a
# juce::AudioIODevice* argument is required, FakeAudioIODevice.h stands in
# for one and never touches a driver. Only AudioIo::start(), called from the
# M1 tests per the plan's own instruction, may open whatever default device
# this machine happens to have -- every other test in this directory never
# calls it.
add_executable(rta_platform_juce_tests
    test_audio_io_contract.cpp
    test_audio_io_reconfig.cpp
)

target_link_libraries(rta_platform_juce_tests PRIVATE
    rta::platform Catch2::Catch2WithMain)

# No `include(Catch)` here either, for the same reason platform/tests/CMakeLists.txt
# gives: that CMake module -- and the CMAKE_MODULE_PATH entry it needs -- is
# loaded once, by core/tests/CMakeLists.txt, and CMake function/macro
# definitions (unlike variables) are global for the rest of the configure
# run, not directory-scoped. Root CMakeLists.txt orders
# add_subdirectory(core/tests) before RTA_BUILD_APP's block runs, for exactly
# this reason.
#
# TEST_PREFIX makes every ctest case name contain "audio_io", so
# `ctest -R audio_io` selects this whole target without relying on Catch2
# tag syntax surviving into the ctest test name (catch_discover_tests names
# each ctest case after the TEST_CASE name, not its tags).
catch_discover_tests(rta_platform_juce_tests TEST_PREFIX "audio_io/")
