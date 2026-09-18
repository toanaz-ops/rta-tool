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
    # test_atomic_shared_ptr.cpp: the publish primitive itself
    # (measure/AtomicSharedPtr.h), header-only and JUCE-free. Two
    # implementations hide behind one interface and only one is compiled per
    # toolchain, so it is tested HERE -- in the RTA_BUILD_APP=OFF target CI
    # runs on all three operating systems -- rather than anywhere that only
    # builds with the app.
    test_atomic_shared_ptr.cpp
    # test_eq_session.cpp: lane L7-EQ task E (record docs/dsp/
    # 2026-09-06-l7-auto-eq.md sec.2, sec.7) -- the app session model, the
    # coherence trust mask and the FilterSpec text export, all JUCE-free like
    # OutputPolicy/DelayLocator, so they live here rather than in
    # app/tests_juce.
    test_eq_session.cpp
    # test_eq_session_lifecycle.cpp: task E's decline / re-measure / export
    # lifecycle, split from test_eq_session.cpp when the round-2 verify cases
    # pushed it past the 400-line cap. Shared fixtures in EqSessionFixture.h.
    test_eq_session_lifecycle.cpp
    # test_eq_trust_export.cpp: task E's other two subjects -- the coherence
    # trust mask (record sec.4.1) and the FilterSpec text format (sec.7) --
    # split out of test_eq_session.cpp when that file grew past one job.
    test_eq_trust_export.cpp
    # test_eq_verify.cpp: lane L7-EQ task F (record sec.8) -- VERIFY over a
    # real rta::platform::OutputEngine with no device, same shape as
    # test_delay_locator.cpp.
    test_eq_verify.cpp
    # test_crossover_topology.cpp: lane L7-ALIGN task D (record docs/dsp/
    # 2026-09-06-l7-alignment-wizard.md Sec.3, Sec.10.2) -- the topology to
    # expected-offset lookup, JUCE-free like OutputPolicy, so it lives here.
    test_crossover_topology.cpp
    # test_virtual_trace.cpp: lane L7-ALIGN task G (record Sec.5, Sec.10.10)
    # -- the one dB<->complex conversion point, and the four NEGATIVE
    # properties that are the reason the type exists. JUCE-free, so it lives
    # here beside test_trace.cpp.
    test_virtual_trace.cpp
    # Lane L7-ALIGN task H (record Sec.2, Sec.7, Sec.9), FOUR files: one was
    # 538 lines, and the refusal-coverage case added after PR #9's verifier
    # pushed a second past 400. Shared fixtures in AlignmentWizardFixture.h,
    # the shared structural-scan reader in CodeLines.h.
    #   ...wizard.cpp          the L7-OUT solo sequence, over a REAL
    #                          OutputEngine with no device
    #   ...wizard_refusals.cpp every way OUT of that sequence has a name, and
    #                          every one of the nine is reached
    #   ...wizard_verdict.cpp  the fit: the A = high-pass-side contract, the
    #                          comparison against the ASKED topology, low R
    #   ...wizard_signals.cpp  the polarity table that asks instead of picking,
    #                          and the structural scans
    test_alignment_wizard.cpp
    test_alignment_wizard_refusals.cpp
    test_alignment_wizard_verdict.cpp
    test_alignment_wizard_signals.cpp
    # test_crossover_surface.cpp: lane L7-ALIGN task I (record Sec.6) -- the
    # G18 surface model, JUCE-free so it is proven OFF on all three CI
    # operating systems. The specimen half is checked by rtatool_snapshot.
    test_crossover_surface.cpp
    # test_crossover_surface_objective.cpp: rows I4/I5 -- "no objective exists"
    # -- split out when three rounds of verification grew the declaration scan
    # into its own subject.
    test_crossover_surface_objective.cpp
    # --- lane L-API (docs/plans/2026-09-17-remote-api-impl-plan.md) ---
    # The read-only remote API's pure half: the wire's number format, the
    # request-shape policy and the serialiser. All framework-free and all in
    # the RTA_BUILD_APP=OFF target, which is the only configuration CI runs
    # (record sec.15 R15) -- so the whole surface is proven on three OSes.
    test_api_json.cpp
    test_api_policy.cpp
    test_api_limits.cpp
    test_api_serialise.cpp
    test_api_serialise_spatial.cpp
    test_api_schema.cpp
    # test_api_server.cpp: Task I. The server over a REAL loopback socket,
    # here rather than in app/tests_juce because sec.15 R15 put ApiServer on
    # a std::thread -- so the Host check, its ORDERING, the Bearer path, the
    # CORS posture, 413, 304 and shutdown all run on three CI OSes. The test
    # client is app/tests/RawHttpClient.h (raw sockets, ~40 lines of logic):
    # an httplib::Client would be a SECOND includer of <httplib.h> and
    # no_server_library_outside_api permits exactly one.
    test_api_server.cpp
    # test_api_server_bind.cpp: the same task, split along the seam that made
    # the file above long -- HOW it binds and WHEN it refuses, rather than
    # what it answers. It carries the FIXED-port case, which is the branch the
    # shipped configuration takes and which every case in the other file
    # (settings.port == 0) leaves untouched.
    test_api_server_bind.cpp
    # test_api_server_refusals.cpp: the station-5 verify pass on PR #18 --
    # every way OUT of the request path and IN WHAT ORDER, plus the method
    # surface `Allow` advertises. Same seam the repo already uses for
    # test_alignment_wizard_refusals.cpp: "which refusal, and does it cost
    # the legitimate client anything" is a different question from "does the
    # refusal happen at all", and the answer to the first one was no.
    test_api_server_refusals.cpp
    # --- L6a Wave 0 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md) -------
    # AllocationProbe.cpp: task W0-B0. THE ONE definition of the replaced
    # global operator new/delete in this binary -- a replaceable global
    # allocation function is one definition per program, so test_average_
    # group.cpp, test_spl_meter.cpp and every later measuring file share this
    # one rather than each carrying a copy that would not link. The C++17
    # over-aligned set is replaced here too, so an alignas(64) type such as
    # rta::dsp::RingBuffer is counted rather than invisible.
    AllocationProbe.cpp
    # test_allocation_probe.cpp: the probe's OWN cases (B0b, B0c), split out of
    # test_average_group.cpp during the macOS CI fix (run 35306075307). "Does
    # the counting allocator work" is a different subject from "is publish
    # churn O(1) in N", and B0c grew a defence against clang eliding the
    # allocation it measures -- libc++ reaches the heap through
    # __builtin_operator_new, which the optimiser is permitted to remove.
    test_allocation_probe.cpp
    # test_spl_meter.cpp + SplMeter.cpp: task W0-B -- the per-channel SPL
    # chain (weighting, two detectors, C-peak, an overload run that survives a
    # block boundary), JUCE-free so it is proven OFF on all three CI
    # operating systems. SplConfig.h is header-only, no .cpp.
    test_spl_meter.cpp
    # test_spl_publish.cpp: task W0-C -- SPL reaches the published Snapshot
    # (one optional block, no wall clock, absence when nothing is logging).
    test_spl_publish.cpp
    # test_spl_seam.cpp: task W0-E -- the 3.0103 dB seam, closed form. Carries
    # the record §13 Q1 SCOPE DEFAULT: convert once at the meter seam,
    # label both, change nothing that exists. No source file changes.
    test_spl_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.cpp
    # SplSession.cpp: task W0-D's session state -- JUCE-free on purpose, so
    # the block clock, the gap arithmetic and the window are proven OFF on
    # all three CI operating systems while AnalysisThread keeps only the
    # three lines that are genuinely about the drain.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.cpp
    # test_spl_session.cpp: its own OFF-build fixtures.
    test_spl_session.cpp
    # --- end L6a Wave 0 ---------------------------------------------------
    # --- L6a Wave 1 (task W1-E, record section 7a) ------------------------
    # test_spl_criteria.cpp + SplCriteria.h: the three 140s as three NAMED
    # criteria, because three frameworks print that number and none of them
    # means the same thing by it -- an OSHA peak that names no weighting and
    # applies only to impulsive noise, an EU dB(C) peak, and a NIOSH dBA
    # LEVEL ceiling. It also carries the app half of SPL-R7: the two shipped
    # dose presets' q. SplCriteria.h is header-only, no .cpp.
    test_spl_criteria.cpp
    # --- end L6a Wave 1 ---------------------------------------------------
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
            "-DGLOBS=${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Levels.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Snapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SnapshotSource.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AtomicSharedPtr.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PlotGeometry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/Readouts.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Trace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TraceDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceBlobCodec.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionStore.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/TraceLibrary.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/RepaintGate.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PhaseUnwrap.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/AxisMetrics.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/BodeLayout.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PhaseDecimator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CoherenceAlpha.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/PairedDrain.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticImpairment.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Workspace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/PaneRegistry.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/ColumnMap.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalyserPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodecDetail.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionDecode.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RoutingPlan.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AverageGroup.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/LevelAlign.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CaptureSequencer.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/OutputPolicy.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AnalysisPublish.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/FirTextWriter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/RawCaptureBuffer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/DelayLocator.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqTrustMask.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/export/EqTextExport.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/EqVerify.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CrossoverTopology.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/VirtualTrace.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizard.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizard.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/AlignmentWizardSignals.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CrossoverSurface.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/view/CrossoverSurface.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiJson.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSettings.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiPolicy.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiPolicy.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialise.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialise.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiSerialiseSpatial.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiRoutes.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiRoutes.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiServer.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/api/ApiServer.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplConfig.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplCriteria.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplMeter.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.h;${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplSession.cpp;${CMAKE_CURRENT_SOURCE_DIR}/*.h;${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
            -DLABEL=app_measure
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_framework_deps.cmake)
