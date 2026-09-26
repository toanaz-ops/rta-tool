# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The JUCE-free half of app/ -- measurement view models and plot geometry --
# compiled straight into a test target. It is NOT linked out of `rtatool`:
# that is a juce_add_gui_app and carries JUCE's module defines, and this
# target must stay buildable with RTA_BUILD_APP=OFF so CI exercises it.
# Registered OUTSIDE the RTA_BUILD_APP guard in the root CMakeLists.txt for
# exactly that reason -- see
# docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §4.2.
include(cmake/base_test_sources.cmake)
include(cmake/api_test_sources.cmake)
include(cmake/spl_test_sources.cmake)
include(cmake/impl_test_sources.cmake)
add_executable(rtatool_analysis_tests
    ${RTA_BASE_TEST_SOURCES}
    ${RTA_API_TEST_SOURCES}
    ${RTA_SPL_TEST_SOURCES}
    ${RTA_IMPL_TEST_SOURCES}
)
target_include_directories(rtatool_analysis_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    # external/: lane L-API Task F (record sec.15 R16). The vendored JSON
    # parser is TEST-ONLY, and no_json_parser_in_shipped_code enforces that
    # with no permitted file at all under app/src. It lives at the repo root
    # rather than under app/, outside every guard's DIRS (sec.15 R3).
    ${CMAKE_SOURCE_DIR}/external)
# RTA_REPO_ROOT: L7-ALIGN task D6 scans core/ from the test itself, so the
# "core stays topology-free" rule runs in CI on all three operating systems
# instead of living in a grep somebody has to remember to type.
# RTA_API_GOLDEN_DIR: lane L-API Task D. A golden the test finds by a
# compiled-in absolute path runs from any working directory on any of the
# three CI operating systems -- the same reasoning as core/tests's
# RTA_GOLDEN_DIR, on the same target as RTA_REPO_ROOT above.
target_compile_definitions(rtatool_analysis_tests PRIVATE
    RTA_REPO_ROOT="${CMAKE_SOURCE_DIR}"
    RTA_API_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(rtatool_analysis_tests PRIVATE
    # rta::platform_types: RoutingPlan.h (task B2) is a pure function over
    # rta::platform::ChannelConfig, JUCE-free like the rest of this target --
    # ChannelConfig.h lives in that target, not rta::core.
    #
    # rta::core is deliberately NOT named here: platform/types/CMakeLists.txt:31
    # links it PUBLIC, so both its include paths and its archive arrive
    # transitively. Naming it as well put librta_core.a on the link line twice,
    # and Apple's ld warned `ignoring duplicate libraries:
    # '../../core/librta_core.a'` on macos-latest.
    rta::platform_types Catch2::Catch2WithMain
    # --- lane L-API Task I --------------------------------------------------
    # rta_httplib is the vendored amalgamated header's INTERFACE target, and
    # on WIN32 it carries ws2_32 with it. This target had never linked Winsock
    # before -- nothing in the RTA_BUILD_APP=OFF half of the repository had
    # opened a socket -- so the link cost of sec.15 R15 arrives here.
    rta_httplib)

# No `include(Catch)` here -- same reasoning as platform/tests/CMakeLists.txt:
# core/tests/CMakeLists.txt loads that CMake module once for the whole
# configure run (function/macro definitions are global, not directory-scoped),
# and root CMakeLists.txt keeps core/tests ordered before this directory for
# exactly that reason.
catch_discover_tests(rtatool_analysis_tests)

# --- The one-process run, which catch_discover_tests does not give us ------
# `catch_discover_tests` registers one ctest test per Catch2 case and
# re-invokes the binary once per case with that case's name as a filter. Every
# case therefore gets a FRESH process, which is usually a feature and was here
# a blindfold: `rta::test`'s byte counter is a program-global, so an order
# dependence between the [allocationprobe] cases is invisible to ctest by
# construction. Measured on PR #22 -- `rtatool_analysis_tests
# "[allocationprobe]" --order decl` failed `8192 == 0` while all three ctest
# entries for the same three cases were green.
#
# So the tag gets run the way a developer and every mutation check run it: one
# process, all three cases, sharing one counter. BOTH orders, because they
# expose opposite things -- under `decl` the anchor case runs before the reset
# case and leaves residue in the counter; under `lex` the reset case runs
# first and the residue travels the other way. One of the two would have
# stayed green on its own.
#
# `--order rand` is deliberately NOT registered: Catch2 seeds it afresh each
# run, so it would be a test whose subject changes between invocations. It is
# worth running by hand (`--order rand --rng-seed <n>`) and a failing seed
# belongs here as a third entry, named.
foreach(RTA_AP_ORDER decl lex)
    add_test(NAME allocation_probe_one_process_order_${RTA_AP_ORDER}
             COMMAND rtatool_analysis_tests "[allocationprobe]"
                     --order ${RTA_AP_ORDER})
endforeach()

# --- Which AtomicSharedPtr branch did this toolchain take? -----------------
# AtomicSharedPtr.h compiles one of two implementations and only one runs on a
# given machine, so "the tests pass" is silent about WHICH one was exercised.
# It has to be said somewhere a CI log actually shows.
#
# Not from inside a test: CI runs `ctest --output-on-failure`, which prints
# nothing at all for a passing test, so a WARN or a std::cout in the binary
# reaches no log unless something has already gone wrong. The Configure step
# is the channel that is always printed, on every runner, in full.
#
# The header answers for itself rather than CMake re-deriving the rule -- the
# probe #includes it and static_asserts on RTA_ATOMIC_SHARED_PTR_IS_STD. Both
# directions are probed and cross-checked, so a probe that fails to build for
# some unrelated reason reports "undetermined" instead of quietly claiming the
# fallback.
include(CheckCXXSourceCompiles)

# A function, NOT block(). block() is CMake 3.25 and the root project declares
# a 3.22 floor, so it would have been a configure error on a 3.22..3.24 CMake
# -- caught in review, not by any runner here, because every machine and every
# GitHub image in play is newer. A function gets the same variable scoping on
# 3.22, and the floor stays where the rest of the project put it.
function(rta_report_atomic_shared_ptr_branch)
    set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    set(CMAKE_REQUIRED_QUIET TRUE)
    if(RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
        set(CMAKE_REQUIRED_DEFINITIONS -DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
    endif()

    # check_cxx_source_compiles memoises its answer in an INTERNAL cache
    # variable and will not re-run while that variable exists -- and it does
    # NOT invalidate it when CMAKE_REQUIRED_DEFINITIONS changes. So flipping
    # RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK in an EXISTING build directory used
    # to leave both probes at their first answers: the configure log printed
    # "forcing the fallback" and "C++20 specialisation" in the same run while
    # the binary being built was the fallback. A status line that can contradict
    # itself is worse than no status line, since it is the one thing a reader
    # has no way to double-check.
    #
    # Dropped from the cache before every probe, rather than keyed on the
    # option's value. Keying on the option would fix the flip and nothing else:
    # the answer also depends on the compiler and the standard library, so an
    # Xcode or GCC upgrade under an existing build dir would go on reporting
    # the old toolchain's answer. Two trivial try_compiles per configure is the
    # right price for a line that is always about THIS configure.
    unset(RTA_ASP_PROBE_SAYS_STD CACHE)
    unset(RTA_ASP_PROBE_SAYS_FALLBACK CACHE)

    check_cxx_source_compiles("
        #include \"measure/AtomicSharedPtr.h\"
        static_assert(RTA_ATOMIC_SHARED_PTR_IS_STD == 1);
        int main() { return 0; }"
        RTA_ASP_PROBE_SAYS_STD)
    check_cxx_source_compiles("
        #include \"measure/AtomicSharedPtr.h\"
        static_assert(RTA_ATOMIC_SHARED_PTR_IS_STD == 0);
        int main() { return 0; }"
        RTA_ASP_PROBE_SAYS_FALLBACK)

    if(RTA_ASP_PROBE_SAYS_STD AND NOT RTA_ASP_PROBE_SAYS_FALLBACK)
        message(STATUS "AtomicSharedPtr: C++20 std::atomic<shared_ptr> specialisation")
    elseif(RTA_ASP_PROBE_SAYS_FALLBACK AND NOT RTA_ASP_PROBE_SAYS_STD)
        message(STATUS "AtomicSharedPtr: std::atomic_* free-function fallback (no P0718 here)")
    else()
        message(WARNING
            "AtomicSharedPtr: branch undetermined -- the probe compiled both ways or "
            "neither (std=${RTA_ASP_PROBE_SAYS_STD} fallback=${RTA_ASP_PROBE_SAYS_FALLBACK}). "
            "The wrapper still works; this line just cannot say which half of it runs.")
    endif()
endfunction()
rta_report_atomic_shared_ptr_branch()

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
#
# The test-side headers in THIS directory are globbed rather than named, and
# that asymmetry is deliberate: app/tests is JUCE-free in full (the JUCE half
# of app/'s tests lives in app/tests_juce), so there is no file here to keep
# out of the scan, and a glob covers the next fixture header without anyone
# remembering to list it. Until 2026-09-16 no header here was scanned at all.
add_test(NAME measure_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            "-DGLOBS=${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Levels.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Snapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSnapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SnapshotSource.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AtomicSharedPtr.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PlotGeometry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/Readouts.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Trace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TraceDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceBlobCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/RepaintGate.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/AxisMetrics.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/BodeLayout.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PhaseDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CoherenceAlpha.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PairedDrain.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticImpairment.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Workspace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PaneRegistry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/ColumnMap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodecDetail.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionDecode.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RoutingPlan.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/LevelAlign.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/OutputPolicy.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirTextWriter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RawCaptureBuffer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqTrustMask.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/EqTextExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizard.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizard.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizardSignals.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CrossoverSurface.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CrossoverSurface.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiJson.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSettings.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiPolicy.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiPolicy.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialise.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialise.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialiseSpatial.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiRoutes.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiRoutes.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiServer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiServer.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplConfig.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplCriteria.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplHistory.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplHistory.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplAlarms.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplAlarms.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLog.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogWriter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplSessionHeader.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/SplStrip.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CalibrationSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CalibrationSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureTimeout.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReport.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportStyle.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportScript.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportHash.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportSections.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportSections.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportHistorySections.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplChannelState.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplChannelState.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogPipeline.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogPipeline.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplLoggingDecision.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSessionFolderName.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplCalibrationRecord.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogHeaderParse.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportPayloadBuilder.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportPayloadBuilder.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CalibrationChannel.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CalibratedSplConfig.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisThreadSplState.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PaneSelectorDecision.h;${CMAKE_CURRENT_SOURCE_DIR}/*.h;${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
            -DLABEL=app_measure
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_framework_deps.cmake)

# --- Honesty guard, grown to the report templates (SPL-R9) -----------------
# core_makes_no_class_1_claim (core/tests/CMakeLists.txt) polices core/'s
# weighting/meter track only. The SPL report is exactly the artefact a reader
# takes away and could act on, so a "Class 1" claim landing in its HTML/CSS/JS
# templates is the same unverifiable-conformance defect one layer up. This
# reuses check_no_conformance_claim.cmake via its GLOBS escape hatch (the same
# mechanism measure_has_no_framework_deps above reuses from
# check_no_framework_deps.cmake) rather than adding a second, divergent
# regex -- one pattern, two independently-scoped tests, two independent
# `OK (N files scanned)` counts, so a change to one scope cannot silently
# shrink the other.
#
# Scope is the SplReport* family only (SplReport.h/.cpp, SplReportHash.cpp,
# SplReportScript.h, SplReportSections.h/.cpp, SplReportStyle.h) -- the
# rendered report and its inlined CSS/JS -- not every file under
# app/src/export/ (SplLog*.h/.cpp and the FIR/EQ exporters make no SPL
# conformance claim of any kind and are not this guard's business, same
# argument as check_no_conformance_claim.cmake's own core/ scope comment).
#
# SPL-R10: SplReportStyle.h's CSS intentionally contains no selector shaped
# like "class" + separator + 0/1/2 (A8) -- Task G shows this guard red on a
# deliberately-added `.class-1 {}` rule and green after reverting it.
#
# EXEMPTIONS (PR #30 fix round, LOW): only "core_makes_no_class_1_claim" --
# SplReportStyle.h's own SPL-R10 comment names that guard by its identifier.
# NOT "report_makes_no_class_1_claim" (this guard's own name): nothing in
# this scope currently references it, and check_no_conformance_claim.cmake
# FATAL_ERRORs on an exemption that matches zero files -- listing it here
# unearned would break this very test, which is the point.
add_test(NAME report_makes_no_class_1_claim
    COMMAND ${CMAKE_COMMAND}
            "-DGLOBS=${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReport*.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReport*.cpp"
            -DTEST_NAME=report_makes_no_class_1_claim
            "-DSCOPE_LABEL=app/src/export's report templates"
            -DEXEMPTIONS=core_makes_no_class_1_claim
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_conformance_claim.cmake)
