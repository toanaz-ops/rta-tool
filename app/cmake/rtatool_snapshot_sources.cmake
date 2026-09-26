# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/CMakeLists.txt -- pure
# relocation. The `rtatool_snapshot` offscreen-render tool's own source list
# -- see tools/snapshot.cpp and app/CMakeLists.txt's own "Offscreen snapshot
# tool" comment for why this target exists and duplicates several sources
# `rtatool` also lists (no shared static library: JUCE_MODULE_AVAILABLE_*
# defines are per juce_add_*_app target).
set(RTATOOL_SNAPSHOT_SOURCES
    src/dev/preview/PreviewFurniture.cpp
    src/dev/preview/TransferFunctionPreview.cpp
    src/dev/preview/TargetMatchPreview.cpp
    src/dev/preview/PhaseAlignPreview.cpp
    # SplPreview.cpp: lane L6a task W2-D's SPL strip preview, over canned data
    # (record §4, §11) -- needed here for preview-spl.png below. SplView.cpp
    # is what the live pane draws with; the preview does not need it.
    src/dev/preview/SplPreview.cpp
    ${CMAKE_SOURCE_DIR}/tools/snapshot.cpp
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
    # length cap. Needed here for transfer.png below.
    src/view/TransferView.cpp
    # TransferViewDraw.cpp is TransferView.cpp's own drawing-helper split
    # (task B0), needed here for the same reason.
    src/view/TransferViewDraw.cpp
    src/view/TransferRibbon.cpp
    # MtwLayer.cpp is the MTW-specific per-band alpha and seam drawing (task 7
    # of the MTW plan), needed here for the specimen render.
    src/view/MtwLayer.cpp
    # MtwReadout.cpp is the per-band integration-seconds strip (record §5,
    # station-4 fix pass finding 1), needed here for the same reason.
    src/view/MtwReadout.cpp
    # TransferSourceToggle.cpp is the per-plot MTW/Fixed control, needed here
    # for the same reason -- TransferView owns one per pane unconditionally.
    src/view/TransferSourceToggle.cpp
    # WorkspaceView.cpp is task 10's pane container (decision 6), needed here
    # for workspace.png. Deliberately does NOT include RtaView.h/
    # TransferView.h itself -- see its own class comment.
    src/view/WorkspaceView.cpp
    # TraceLibrary.cpp is what StoredTraceLayer.cpp calls entry()/trace() on.
    # Header-only up to Task 5 because only rtatool_analysis_tests compiled it;
    # the moment the VIEW reads the library, both GUI binaries need the
    # definitions linked. It pulls in nothing else -- its only include is its
    # own header.
    src/trace/TraceLibrary.cpp
    # Only what the tool's rta-view.png / transfer.png renders (via
    # makeSyntheticSnapshot / makeSyntheticTransfer) need: the offline,
    # thread-free measurement path. PhaseUnwrap.cpp is TransferView's unwrap
    # toggle (decision 4), JUCE-free but named explicitly for the same
    # one-static-library reason as Analyser.cpp above it.
    src/measure/Analyser.cpp
    # AnalyserPublish.cpp is Analyser.cpp's publish()-site block-builder split
    # (task B0) -- needed here for the same one-static-library reason as
    # Analyser.cpp itself.
    src/measure/AnalyserPublish.cpp
    src/measure/SyntheticSnapshot.cpp
    src/measure/PhaseUnwrap.cpp
    # CrossoverSurface.cpp + CrossoverTopology.cpp + VirtualTrace.cpp: lane
    # L7-ALIGN task I. PhaseAlignPreview.cpp is no longer a canned-data mockup
    # -- it draws a real rta::view::CrossoverSurface over a synthetic BW4 pair
    # -- and this target names every source ONE BY ONE, with no glob. All three
    # must be here or preview-phase.png fails to link with undefined symbols
    # (plan task I "Files", verifier defect 6; carried into Wave 3b by PR #8's
    # "take to the orchestrator" list, item 6).
    src/view/CrossoverSurface.cpp
    src/measure/CrossoverTopology.cpp
    src/trace/VirtualTrace.cpp
    # main-live.png additionally renders the real MainComponent in synthetic
    # mode (T10/T11's own composition root, plan §0 item 4's "no hardware at
    # all, on CI, and offscreen" made literal) -- which needs the live-device
    # path's JUCE-owning sources too, and AudioIo along with it. This is the
    # one deviation from plan §3.6's "must not link juce_audio_devices": that
    # line was written before this tool also had to construct MainComponent
    # itself, not only feed a StaticSnapshotSource into RtaView.
    src/MainComponent.cpp
    # MainComponentLayout.cpp: see the matching entry in rtatool's own source
    # list above -- fix round PRs #36/#37 item 3, needed here for the same
    # reason MainComponentDelay.cpp is (main-live.png constructs the real
    # MainComponent, which now defines paint()/resized() in that file).
    src/MainComponentLayout.cpp
    src/MainComponentDelay.cpp
    # MainComponentPanes.cpp: owner decision 2026-09-26's pane selector,
    # needed here for the same reason MainComponentDelay.cpp is -- main-live
    # (and main-live-spl) construct the real MainComponent, which now owns
    # this file's wirePaneSelectorButtons()/selectPaneView().
    src/MainComponentPanes.cpp
    # PaneFactory.cpp: makePaneFactory's own translation unit (fix round, PR
    # #26), needed here since MainComponent.cpp calls it. SplView.cpp is
    # what its Spl branch constructs.
    src/PaneFactory.cpp
    src/view/SplView.cpp
    # MainComponentCalibration.cpp / CalibrationSession.cpp: L6a Wave 3 task
    # W3-B, needed here for the same reason MainComponentDelay.cpp is --
    # main-live.png constructs the real MainComponent.
    src/MainComponentCalibration.cpp
    src/measure/CalibrationSession.cpp
    # MainComponentSpl.cpp / SplLogPipeline.cpp / SplLogWriter.cpp: L6a task
    # W2-E2a, needed here for the same reason MainComponentCalibration.cpp is
    # -- main-live.png constructs the real MainComponent, which now calls
    # enableSplLogging. See the matching entry in rtatool's own source list
    # above for why the export/ pair has to be named explicitly here too.
    src/MainComponentSpl.cpp
    src/export/SplLogPipeline.cpp
    src/export/SplLogWriter.cpp
    # SplReportPayloadBuilder.cpp / SplReport.cpp / SplReportHash.cpp /
    # SplReportSections.cpp: see the matching entry in rtatool's own source
    # list above -- task W2-E2b part B, needed here for the same reason.
    src/export/SplReportPayloadBuilder.cpp
    src/export/SplReport.cpp
    src/export/SplReportHash.cpp
    src/export/SplReportSections.cpp
    src/export/SplReportHistorySections.cpp
    src/view/DevicePanel.cpp
    src/view/ChannelRoleTable.cpp
    src/view/RoutingMatrix.cpp
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
    # SplHistory.cpp / SplAlarms.cpp / SplChannelState.cpp: see the same
    # entries in rtatool's own source list above -- this target names every
    # source one by one with no glob, so all three have to be listed here
    # too or the snapshot tool fails to link.
    src/measure/SplHistory.cpp
    src/measure/SplAlarms.cpp
    src/measure/SplChannelState.cpp
    # --- end L6a Wave 2 task W2-E1 ------------------------------------------
    src/measure/AnalysisPublish.cpp
    src/measure/AverageGroup.cpp
    src/measure/SyntheticInput.cpp
    # Lane L-API Task J: this target constructs MainComponent too (see the
    # main-live.png comment above), so it needs the ApiServer definitions for
    # the same reason rtatool does. This target names every source one by one
    # with no glob, so all four have to be here or preview renders fail to
    # link with undefined symbols.
    src/api/ApiServer.cpp
    src/api/ApiRoutes.cpp
    src/api/ApiPolicy.cpp
    src/api/ApiSerialise.cpp
    src/api/ApiSerialiseSpatial.cpp
)
