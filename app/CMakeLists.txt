# SPDX-License-Identifier: AGPL-3.0-or-later
#
# rtatool -- the composition root. It wires rta_core (measurement),
# rta_platform (device I/O) and az_ui (design system) together and owns the
# view models. It contains no DSP and no drawing of its own.

# --- ASIO ---------------------------------------------------------------
# Steinberg dual-licensed the ASIO SDK under GPLv3 in October 2025, which is
# what makes low-latency multichannel input legal in an AGPL application. The
# SDK is still not vendored here: it is pointed at, so the licence notice stays
# simple and the download stays Steinberg's to serve.
set(RTA_ASIO_SDK_PATH "" CACHE PATH "Path to the Steinberg ASIO SDK")

if(WIN32 AND RTA_ENABLE_ASIO)
    if(EXISTS "${RTA_ASIO_SDK_PATH}/common/asio.h")
        message(STATUS "ASIO SDK found at ${RTA_ASIO_SDK_PATH}")
    else()
        message(FATAL_ERROR
            "RTA_ENABLE_ASIO is ON but no ASIO SDK at '${RTA_ASIO_SDK_PATH}'.\n"
            "Set -DRTA_ASIO_SDK_PATH=<dir containing common/asio.h>, "
            "or configure with -DRTA_ENABLE_ASIO=OFF.")
    endif()
endif()

# --- Typefaces ----------------------------------------------------------
# Embedded, not resolved from the machine. Naming a face in a fallback list
# gives the intended design on a machine that happens to have it and something
# else everywhere else, which is a lottery rather than a design. All three are
# OFL; the licences ship beside the files in assets/fonts/.
juce_add_binary_data(RtaToolFonts SOURCES
    ${CMAKE_SOURCE_DIR}/assets/fonts/SairaCondensed-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/SairaCondensed-Bold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexSans-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf
)

juce_add_gui_app(rtatool
    PRODUCT_NAME  "RTA Tool"
    COMPANY_NAME  "AZ Soundtech"
    BUNDLE_ID     "com.azsoundtech.rtatool"
    VERSION       ${PROJECT_VERSION}
)

juce_generate_juce_header(rtatool)

target_sources(rtatool PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponentDelay.cpp
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

# app/src as an include root: app/src/measure and app/src/view headers
# include each other as "measure/Foo.h" / "view/Foo.h" (see app/tests/
# CMakeLists.txt, which already does the same for the JUCE-free half), not
# as a path relative to the including file.
target_include_directories(rtatool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(rtatool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:rtatool,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:rtatool,JUCE_VERSION>"
    $<$<AND:$<BOOL:${WIN32}>,$<BOOL:${RTA_ENABLE_ASIO}>>:JUCE_ASIO=1>
)

if(WIN32 AND RTA_ENABLE_ASIO)
    target_include_directories(rtatool PRIVATE "${RTA_ASIO_SDK_PATH}/common")
endif()

target_link_libraries(rtatool PRIVATE
    RtaToolFonts
    az_ui
    rta::core
    rta::platform
    # rta_httplib: lane L-API Task J. The vendored server header's INTERFACE
    # target, carrying ws2_32 on WIN32 -- ApiServer.cpp is compiled into this
    # binary now, and it is the one TU that includes <httplib.h>.
    rta_httplib
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- Offscreen snapshot tool --------------------------------------------
# Renders components to PNG with createComponentSnapshot so GUI work can be
# reviewed without a screen. See tools/snapshot.cpp for why this beats
# capturing the running app.
#
# SpecimenComponent.cpp and AppTypefaces.cpp are listed in both targets rather
# than factored into a static library: a plain library does not receive the
# JUCE_MODULE_AVAILABLE_* definitions that juce_add_gui_app injects, so the
# az_ui include would fail. Two short source lists is the cheaper problem.
juce_add_console_app(rtatool_snapshot
    PRODUCT_NAME "RTA Tool Snapshot"
    COMPANY_NAME "AZ Soundtech"
)

target_sources(rtatool_snapshot PRIVATE
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
    src/MainComponentDelay.cpp
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

target_include_directories(rtatool_snapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(rtatool_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    $<$<AND:$<BOOL:${WIN32}>,$<BOOL:${RTA_ENABLE_ASIO}>>:JUCE_ASIO=1>
)

if(WIN32 AND RTA_ENABLE_ASIO)
    target_include_directories(rtatool_snapshot PRIVATE "${RTA_ASIO_SDK_PATH}/common")
endif()

target_link_libraries(rtatool_snapshot PRIVATE
    RtaToolFonts
    az_ui
    rta::core
    rta::platform
    # rta_httplib: lane L-API Task J, for the same reason as rtatool above --
    # this target constructs MainComponent, which owns an ApiServer.
    rta_httplib
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
