# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/CMakeLists.txt (over the
# 400-line hard cap) -- pure relocation, no source added, dropped or
# reordered. The `rtatool` GUI app's own source list -- every comment stays
# with the file it explains.
set(RTATOOL_SOURCES
    src/Main.cpp
    src/MainComponent.cpp
    # MainComponentLayout.cpp: paint()/resized() -- fix round PRs #36/#37
    # item 3's own 400-line-cap split, same shape as MainComponentDelay.cpp
    # below.
    src/MainComponentLayout.cpp
    src/MainComponentDelay.cpp
    # MainComponentPanes.cpp: owner decision 2026-09-26's pane selector --
    # MainComponent.cpp's own 400-line-cap split, same shape as
    # MainComponentDelay.cpp/MainComponentCalibration.cpp.
    src/MainComponentPanes.cpp
    # PaneFactory.cpp: makePaneFactory's own translation unit, split out of
    # MainComponent.cpp (fix round, PR #26) so a test can link it without
    # MainComponent's other dependencies. See PaneFactory.h.
    src/PaneFactory.cpp
    # MainComponentCalibration.cpp: L6a Wave 3 task W3-B's own 400-line-cap
    # split (record docs/dsp/2026-09-16-spl-pro-l6a.md §8), same shape as
    # MainComponentDelay.cpp. CalibrationSession.cpp is JUCE-free and also
    # compiled into rtatool_analysis_tests.
    src/MainComponentCalibration.cpp
    src/measure/CalibrationSession.cpp
    # MainComponentSpl.cpp: L6a task W2-E2a's own 400-line-cap split (plan
    # amendment "W2-E -- the wiring nobody was assigned"), same shape as
    # MainComponentDelay.cpp/MainComponentCalibration.cpp. SplLogPipeline.cpp
    # and SplLogWriter.cpp had NO caller anywhere in app/src before this task
    # (AnalysisThread.h now includes SplLogPipeline.h and calls into it) --
    # same "first real caller, must be named explicitly" shape SplHistory.cpp/
    # SplAlarms.cpp already went through for W2-E1, just above.
    src/MainComponentSpl.cpp
    src/export/SplLogPipeline.cpp
    src/export/SplLogWriter.cpp
    # SplReportPayloadBuilder.cpp / SplReport.cpp / SplReportHash.cpp /
    # SplReportSections.cpp: task W2-E2b part B -- MainComponentSpl.cpp's own
    # exportReportClicked() is the first `app/src` caller of the report
    # renderer (plan Wave 4a-A) and its own payload builder; both are
    # JUCE-free and already compiled into rtatool_analysis_tests, but this
    # target names every source one by one, so all five have to be listed
    # here too or this binary fails to link with undefined symbols -- the
    # same "first real caller" shape SplHistory.cpp/SplAlarms.cpp went
    # through for W2-E1 above.
    src/export/SplReportPayloadBuilder.cpp
    src/export/SplReport.cpp
    src/export/SplReportHash.cpp
    src/export/SplReportSections.cpp
    src/export/SplReportHistorySections.cpp
    src/dev/SpecimenComponent.cpp
    src/AppTypefaces.cpp
    src/view/PlotAxes.cpp
    src/view/RtaView.cpp
    src/view/StoredTraceLayer.cpp
    # TraceStroke.cpp is the stroking half StoredTraceLayer.cpp calls
    # strokeMagnitudeExtents()/strokePhaseColumns() on (task 7's split).
    src/view/TraceStroke.cpp
    # TransferView.cpp is the Bode composite (task 8); TransferRibbon.cpp is
    # its ribbon half, split out to keep TransferView.cpp under the file-
    # length cap (see that file's own header comment for why).
    src/view/TransferView.cpp
    src/view/TransferRibbon.cpp
    # TransferViewDraw.cpp is TransferView.cpp's own drawing-helper split
    # (task B0, docs/plans/2026-09-06-L6b-impl-plan.md), moved out ahead of
    # L6b adding more to TransferView.cpp.
    src/view/TransferViewDraw.cpp
    # MtwLayer.cpp is the MTW-specific per-band alpha and seam drawing (task 7
    # of the MTW plan), split out to keep TransferView.cpp under the cap.
    src/view/MtwLayer.cpp
    # MtwReadout.cpp is the per-band integration-seconds strip (record §5,
    # station-4 fix pass finding 1), split out for the same file-length
    # reason MtwLayer.cpp already gives.
    src/view/MtwReadout.cpp
    # TransferSourceToggle.cpp is the per-plot MTW/Fixed control
    # (docs/reports/005-mtw-engine.md "Known gaps"), split out for the same
    # file-length reason MtwLayer.cpp/MtwReadout.cpp already give.
    src/view/TransferSourceToggle.cpp
    # WorkspaceView.cpp is task 10's pane container (decision 6) -- the file
    # that finally attaches TraceLibrary to a real pane. Deliberately does
    # NOT include RtaView.h/TransferView.h itself (see its own class
    # comment); those two are still needed here for MainComponent.cpp's pane
    # factory, which is why they are already above.
    src/view/WorkspaceView.cpp
    # SplView.cpp: lane L6a task W2-D's live SPL pane (record §4, §11). Not
    # yet wired into MainComponent's pane factory -- Wave 2 publishes no
    # history ring for it to draw a strip from (see the class's own comment)
    # -- but compiled here so it builds against real JUCE headers.
    src/view/SplView.cpp
    # TraceLibrary.cpp is what StoredTraceLayer.cpp calls entry()/trace() on.
    # Header-only up to Task 5 because only rtatool_analysis_tests compiled it;
    # the moment the VIEW reads the library, both GUI binaries need the
    # definitions linked. It pulls in nothing else -- its only include is its
    # own header.
    src/trace/TraceLibrary.cpp
    src/view/DevicePanel.cpp
    src/view/ChannelRoleTable.cpp
    # AnalysisThread / SyntheticInput are JUCE-owning (plan §3.4) and belong
    # only to the live-device path -- rtatool_snapshot renders from
    # makeSyntheticSnapshot() instead (see its own target_sources below) and
    # must not link juce_audio_devices.
    src/measure/AnalysisThread.cpp
    # --- L6a Wave 0 task W0-D ---------------------------------------------
    # AnalysisThreadSpl.cpp: AnalysisThread.cpp's SPL half, split off at the
    # 400-line hard cap -- same class, second translation unit, the shape
    # AlignmentWizard.cpp/AlignmentWizardSignals.cpp already ships.
    # SplSession.cpp / SplMeter.cpp: the session and the per-channel chain,
    # both JUCE-free and both also compiled into rtatool_analysis_tests.
    src/measure/AnalysisThreadSpl.cpp
    src/measure/SplSession.cpp
    src/measure/SplMeter.cpp
    # --- end L6a Wave 0 ---------------------------------------------------
    # --- L6a Wave 2 task W2-E1 ---------------------------------------------
    # SplChannelState.cpp: the per-channel history/alarms/dose/Ln state
    # AnalysisThreadSpl.cpp's feedSpl() and AnalysisPublish.cpp's
    # buildSplBlockView() both call into directly -- JUCE-free, same
    # also-compiled-into-rtatool_analysis_tests shape as SplSession.cpp above.
    # SplHistory.cpp (W2-A) and SplAlarms.cpp (W2-B) had NO caller anywhere in
    # app/src before this task (the wiring gap the plan amendment names) and
    # so were never in this list either; SplChannelState.cpp is their first
    # real caller, and both have to be named here now or this target fails to
    # link with undefined symbols.
    src/measure/SplHistory.cpp
    src/measure/SplAlarms.cpp
    src/measure/SplChannelState.cpp
    # --- end L6a Wave 2 task W2-E1 ------------------------------------------
    # AnalysisPublish.cpp is AnalysisThread's own wiring of AverageGroup
    # into its publish path (task F2, record §6) -- JUCE-free, same
    # one-static-library reason as Analyser.cpp below, so it is named
    # explicitly in both GUI binaries' source lists.
    src/measure/AnalysisPublish.cpp
    src/measure/AverageGroup.cpp
    src/measure/SyntheticInput.cpp
    # RoutingMatrix.cpp: task F2 mounts it in MainComponent's rail.
    src/view/RoutingMatrix.cpp
    # Analyser.cpp is JUCE-free and already listed in rtatool_snapshot below,
    # but that is a SEPARATE binary -- AnalysisThread.cpp's calls into
    # rta::measure::Analyser need the definition linked into rtatool too, not
    # only into the snapshot tool. Same reasoning as SpecimenComponent.cpp
    # appearing in both target_sources lists (comment above
    # rtatool_snapshot): one static library would not receive the
    # JUCE_MODULE_AVAILABLE_* defines juce_add_gui_app injects.
    src/measure/Analyser.cpp
    # AnalyserPublish.cpp is Analyser.cpp's publish()-site block-builder split
    # (task B0) -- JUCE-free, same one-static-library reason as Analyser.cpp
    # itself, so it is named explicitly in both GUI binaries' source lists.
    src/measure/AnalyserPublish.cpp
    # PhaseUnwrap.cpp is what TransferView.cpp's unwrap toggle (decision 4)
    # calls into -- JUCE-free, but the same one-static-library caveat above
    # applies, so it is named explicitly in both GUI binaries' source lists.
    src/measure/PhaseUnwrap.cpp
    # --- lane L-API Task J --------------------------------------------------
    # The composition root owns an ApiServer, so both GUI binaries that
    # construct MainComponent need its definitions linked -- the same
    # one-static-library caveat as Analyser.cpp above. ApiPolicy.cpp and the
    # two serialiser TUs come with it: ApiServer.cpp calls straight into them.
    src/api/ApiServer.cpp
    src/api/ApiRoutes.cpp
    src/api/ApiPolicy.cpp
    src/api/ApiSerialise.cpp
    src/api/ApiSerialiseSpatial.cpp
)
