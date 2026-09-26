# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/tests/CMakeLists.txt -- pure
# relocation. Lane L6a's whole SPL feature surface, Waves 0 through 4a plus
# task W2-E2b and its fix round: the per-channel meter, the session, history,
# alarms, the log-writing pipeline, calibration, and the HTML report.
set(RTA_SPL_TEST_SOURCES
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
    # test_allocation_probe_threading.cpp: LOW follow-up batch, item 13 --
    # split out of test_allocation_probe.cpp (436 lines, over the 400-line
    # hard cap) along its own pre-existing seam: the round-4 per-thread
    # ATTRIBUTION case (the only one in the file that spawns a background
    # thread), extended here to cover the ALIGNED operator new overload's own
    # separate per-thread check.
    test_allocation_probe_threading.cpp
    # test_spl_meter.cpp + SplMeter.cpp: task W0-B -- the per-channel SPL
    # chain's WEIGHTING/DETECTOR half (B1, B2, B2b, B5, histogramBaseDb),
    # JUCE-free so it is proven OFF on all three CI operating systems.
    # SplConfig.h is header-only, no .cpp.
    test_spl_meter.cpp
    # test_spl_meter_accounting.cpp: LOW follow-up batch, item 4 -- split out
    # of test_spl_meter.cpp (449 lines, over the 400-line hard cap) along the
    # BLOCK-FLOW/ALLOCATION seam: B3 (overload run across a block boundary),
    # B4 (no allocation), and the two sample-accounting invariants. See that
    # file's own header comment for the split.
    test_spl_meter_accounting.cpp
    # test_spl_publish.cpp: task W0-C -- SPL reaches the published Snapshot
    # (one optional block, no wall clock, absence when nothing is logging).
    # THE CORE CONTRACT ONLY (C1-C4, the two station-4 mirror fields) as of
    # the LOW follow-up batch, items 4/5/11 -- see this file's own header
    # comment for the three-way split of the original 804-line file.
    test_spl_publish.cpp
    # test_spl_publish_metrics.cpp: the other half of that same split -- the
    # metric fold itself (combineBlocks, per-metric windows, Wave 0's dose/Ln
    # absence) and PR #17 verifier defect 1 end to end.
    test_spl_publish_metrics.cpp
    # test_spl_publish_snapshot.cpp: the third of that split --
    # buildPublishedSnapshot end to end, through both the unrouted and the
    # routed branch, and the window-array bound as a GATE over a real
    # SplSession.
    test_spl_publish_snapshot.cpp
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
    # test_spl_session_newly_closed.cpp: split out of test_spl_session.cpp
    # (PR #29 round-3 fix pass step 6, 400-line hard cap) -- the fix round
    # 2026-09-25 item 4 / M5 newlyClosedBlocks-per-chain cases.
    test_spl_session_newly_closed.cpp
    # test_spl_session_metric_cap.cpp: split out of test_spl_session.cpp
    # (PR #29 round-3 fix pass step 6, 400-line hard cap) -- the PR #17
    # verifier defect 1 metric-cap / fillMetricWindows cases.
    test_spl_session_metric_cap.cpp
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
    # --- L6a Wave 2 (record section 4) -------------------------------------
    # test_spl_history.cpp + SplHistory.cpp: task W2-A -- the declared-span
    # ring of Blocks, allocated once from logSpanSeconds and never grown.
    # JUCE-free so it is proven OFF on all three CI operating systems.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplHistory.cpp
    test_spl_history.cpp
    # test_spl_alarms.cpp + SplAlarms.cpp: task W2-B -- alarms over a sliding
    # windowed Leq, with a proxy window whose offset is the operator's.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplAlarms.cpp
    test_spl_alarms.cpp
    # test_spl_log.cpp + SplLogWriter.cpp: task W2-C -- the append-only SPL
    # log (SplLog.h is header-only, the EqTextExport.h precedent).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogWriter.cpp
    test_spl_log.cpp
    # test_spl_log_write_failure.cpp: station-4 fix round, PR #31, round 3,
    # verifier finding 2 (MEDIUM) -- a write() failing AFTER a successful
    # open, not just openSegment() itself. New file rather than growing
    # test_spl_log.cpp, which was already close to the 400-line hard cap.
    test_spl_log_write_failure.cpp
    # test_spl_session_header.cpp: fix round item 7, record §10's "one file
    # per logged channel plus one session header" -- SplSessionHeader.h is
    # header-only content, split out of SplLog.h to stay well clear of the
    # 400-line hard cap; writeSessionHeaderFile is defined in
    # SplLogWriter.cpp, the one file in this family that opens a stream.
    test_spl_session_header.cpp
    # test_spl_strip.cpp: task W2-D's headless half -- the pane name (D1) and
    # the strip's own numbers-in/positions-out geometry (D2, D3). SplStrip.h
    # is header-only, no .cpp (the BodeLayout.h precedent).
    test_spl_strip.cpp
    # test_spl_channel_state.cpp + SplChannelState.cpp: task W2-E1 -- the
    # per-channel SPL state (history, alarms, dose, Ln) the analysis thread
    # owns and updates once per closed block, and the publish fold reads.
    # JUCE-free so it is proven OFF on all three CI operating systems.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SplChannelState.cpp
    test_spl_channel_state.cpp
    # test_spl_channel_state_routing.cpp: split out of test_spl_channel_
    # state.cpp (PR #29 round-3 fix pass step 6, 400-line hard cap) -- the
    # 1a/1b/1c "every consumer reads the chain its own definition names"
    # routing cases, which need a real SplSession to prove.
    test_spl_channel_state_routing.cpp
    # test_spl_channel_state_fixes.cpp: split out of test_spl_channel_
    # state.cpp too (same step 6) -- the round-3 verifier's own steps 3/4/5,
    # following the test_spl_report_fixes.cpp precedent.
    test_spl_channel_state_fixes.cpp
    # test_spl_ln_ticks.cpp: PR #29 round-3 fix pass step 1 (MEDIUM) -- Ln
    # sampled at the detector's own 100 ms clock, never at block closure.
    test_spl_ln_ticks.cpp
    # test_spl_log_pipeline.cpp + SplLogPipeline.cpp: task W2-E2a -- the
    # fixed-capacity SPSC queue per logged channel and its dedicated writer
    # thread, the wiring nobody was assigned (plan amendment "W2-E"). JUCE-
    # free so the queue/writer mechanics are proven OFF on all three CI
    # operating systems; only the caller (AnalysisThread) is JUCE.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplLogPipeline.cpp
    test_spl_log_pipeline.cpp
    # test_spl_log_pipeline_writefailed.cpp: split out of test_spl_log_
    # pipeline.cpp (round 4, PR #31, LOW finding 3, 400-line hard cap) --
    # every writeFailed()-mirror case: findings 4 and 6's own tests plus
    # round 4 finding 1's mutant-M4 case (drainOnce()'s own check, isolated
    # from setupWriters()'s by forcing the stream failure AFTER a
    # successful open rather than at open time).
    test_spl_log_pipeline_writefailed.cpp
    # test_spl_log_pipeline_alloc.cpp: split out of the same file, same
    # reason -- pure relocation this commit; round 4 finding 2 (the next
    # commit) is what changes its body.
    test_spl_log_pipeline_alloc.cpp
    # test_spl_publish_round4.cpp: PR #29 round-4 fix pass items 3/4 -- a NEW
    # file rather than growing test_spl_publish.cpp (over the 400-line cap at
    # the time -- since split three ways, LOW follow-up batch items 4/5/11;
    # this file stays separate regardless, its own subject being round-4's
    # two fields specifically) -- SplBlockView::lnTicksOverflowed and
    # ::blockSecondsBelowRecommendedFloor actually reach the publish.
    test_spl_publish_round4.cpp
    # test_spl_logging_decision.cpp + SplLoggingDecision.h: station-4 fix
    # round, PR #31, verifier findings 1/2 -- the composition root's
    # off/on/epoch-changed decision, pulled out of MainComponentSpl.cpp (JUCE,
    # untestable OFF) into a pure function so all four transitions, including
    # the HIGH finding (epoch changes while still "active"), are proven on
    # all three CI operating systems. SplLoggingDecision.h is header-only.
    test_spl_logging_decision.cpp
    # test_spl_session_folder_name.cpp + SplSessionFolderName.h: station-4 fix
    # round, PR #31, round 3, verifier finding 3 (LOW) -- the folder-name
    # collision property (a rapid device bounce inside the same UTC second),
    # pulled out of MainComponentSpl.cpp into a pure function so it is
    # provable OFF against a fixed instant. Header-only, no .cpp.
    test_spl_session_folder_name.cpp
    # --- end L6a Wave 2 -----------------------------------------------------
    # --- L6a Wave 3 task W3-A (record §8, §13 Q2) --------------------------
    # test_calibration.cpp + CalibrationSession.cpp: calibration as a flow --
    # a start/end pair, a drift, and ISO 1996-2 cl. 5.2's 0.5 dB as the only
    # published criterion. JUCE-free like SplMeter/SplSession, so it is
    # proven OFF on all three CI operating systems.
    test_calibration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/CalibrationSession.cpp
    # test_capture_timeout.cpp + CaptureTimeout.h: fix round, verifier round 1
    # finding 6 -- the one decision that ends a stuck calibration capture's
    # lock on Locate, factored out header-only so it is provable OFF.
    test_capture_timeout.cpp
    # --- end L6a Wave 3 task W3-A -------------------------------------------
    # --- L6a Wave 4a task W4a-A / W3-C (record sec.9, sec.11) ---------------
    # test_spl_report.cpp + SplReport.cpp: the self-contained HTML report --
    # nine sections, the honesty sentence, an integrity hash, and (W3-C) the
    # calibration row. SplReport.h/SplReportStyle.h/SplReportScript.h are
    # header-only, no .cpp of their own.
    test_spl_report.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReport.cpp
    # SplReportHash.cpp / SplReportSections.cpp: split out of SplReport.cpp
    # (PR #28 fix round) along the SHA-256 and section-renderer seams so
    # SplReport.cpp stays clear of the 400-line hard cap. Pure move, no
    # behaviour change.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportHash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportSections.cpp
    # SplReportHistorySections.cpp: split out of SplReportSections.cpp (fix
    # round 3) -- renderCalibration/renderHistory/renderValidity, the three
    # sections the calibration-channel-refusal and marker/excluded-range
    # fixes touch, moved so SplReportSections.cpp stays clear of the
    # 400-line hard cap. Same detail namespace, same SplReportSections.h
    # declarations.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportHistorySections.cpp
    # test_spl_report_fixes.cpp: the independent verifier's six HIGH/MEDIUM
    # findings on PR #28, split out of test_spl_report.cpp to keep that file
    # clear of the 400-line hard cap. SplReportTestSupport.h is the shared
    # fixture header both files include (the CodeLines.h precedent).
    test_spl_report_fixes.cpp
    # test_spl_report_fixes_round3.cpp: PR #32 fix round 3's marker x-scale/
    # excluded-range and calibration-channel-refusal findings, split into its
    # OWN new file (rather than grown into test_spl_report_fixes.cpp, which
    # is already within a few lines of the 400-line cap after round 2).
    test_spl_report_fixes_round3.cpp
    # --- end L6a Wave 4a -----------------------------------------------------
    # --- L6a task W2-E2b (plan amendment "W2-E -- the wiring nobody was
    # assigned", part b) -------------------------------------------------
    # test_spl_calibration_record.cpp + SplCalibrationRecord.h: part A -- the
    # calibration record written into a session folder once the END check
    # completes, round-tripped by parseCalibrationRecord. Header-only.
    test_spl_calibration_record.cpp
    # test_spl_log_header_parse.cpp + SplLogHeaderParse.h: the inverse of
    # SplLog.h's logHeader(), needed only by the report payload builder below
    # -- split out of SplLog.h rather than grown into it (that file was
    # already within a few lines of the 400-line cap). Header-only.
    test_spl_log_header_parse.cpp
    # test_spl_report_payload_builder.cpp + SplReportPayloadBuilder.cpp: part
    # B -- turns a session folder (SplLogWriter's own files, plus an optional
    # calibration record) into the ReportPayload SplReport.cpp already knows
    # how to render.
    test_spl_report_payload_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export/SplReportPayloadBuilder.cpp
    # --- W2-E2b fix round (verifier HIGH/MEDIUM findings) -------------------
    # test_calibration_channel.cpp + CalibrationChannel.h: HIGH -- a ROUTE
    # POSITION is not a CHANNEL NUMBER; resolves the calibration flow's route
    # index to the channel every channel-indexed AnalysisThread call actually
    # needs. Header-only.
    test_calibration_channel.cpp
    # test_calibrated_spl_config.cpp + CalibratedSplConfig.h: MEDIUM (mutant
    # M2) -- the composition-root step "calibration start -> the SplConfig a
    # fresh log should carry", lifted out of MainComponentCalibration.cpp so
    # a dropped offset assignment is caught OFF rather than shipping quietly.
    # Header-only.
    test_calibrated_spl_config.cpp
    # test_spl_publish_calibration_invalid.cpp: MEDIUM (mutant M5) -- a NEW
    # file per this fix round's own instruction not to grow
    # test_spl_publish.cpp further (that file has since been split three ways,
    # LOW follow-up batch items 4/5/11; this file stays separate regardless,
    # its own subject being calibrationInvalid specifically); proves
    # SplBlockView::calibrationInvalid actually reaches the published view.
    test_spl_publish_calibration_invalid.cpp
    # test_spl_report_payload_builder_fixes.cpp: MEDIUM -- record §9 item 7's
    # time history and markers, split out of test_spl_report_payload_
    # builder.cpp (400-line cap); shared fixtures in
    # SplReportPayloadBuilderTestSupport.h.
    test_spl_report_payload_builder_fixes.cpp
    # --- end W2-E2b fix round ------------------------------------------------
    # --- end L6a task W2-E2b -------------------------------------------------
)
