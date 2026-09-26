# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/tests/CMakeLists.txt (over
# the 400-line hard cap) -- pure relocation, no test added, dropped or
# renamed. This is the general measurement/view-model test surface: every
# test_*.cpp registered before lane L-API's own block. See
# app/tests/CMakeLists.txt's own header comment for the other three pieces
# of this split (api/spl/impl) and the ctest -N proof that nothing moved.
set(RTA_BASE_TEST_SOURCES
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
    # test_pane_selector_decision.cpp + PaneSelectorDecision.h: the pane
    # selector's pure half (owner decision 2026-09-26, "the gap" -- SPL and
    # Transfer were built and tested but unreachable from the running app).
    # Routes through the same resolvePaneView a saved session uses, so it
    # lives beside test_workspace.cpp/PaneRegistry.h rather than duplicating
    # its own mapping.
    test_pane_selector_decision.cpp
)
