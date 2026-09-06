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
    # MtwLayer.cpp is the MTW-specific per-band alpha and seam drawing (task 7
    # of the MTW plan), split out to keep TransferView.cpp under the cap.
    src/view/MtwLayer.cpp
    # MtwReadout.cpp is the per-band integration-seconds strip (record §5,
    # station-4 fix pass finding 1), split out for the same file-length
    # reason MtwLayer.cpp already gives.
    src/view/MtwReadout.cpp
    # WorkspaceView.cpp is task 10's pane container (decision 6) -- the file
    # that finally attaches TraceLibrary to a real pane. Deliberately does
    # NOT include RtaView.h/TransferView.h itself (see its own class
    # comment); those two are still needed here for MainComponent.cpp's pane
    # factory, which is why they are already above.
    src/view/WorkspaceView.cpp
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
    src/measure/SyntheticInput.cpp
    # Analyser.cpp is JUCE-free and already listed in rtatool_snapshot below,
    # but that is a SEPARATE binary -- AnalysisThread.cpp's calls into
    # rta::measure::Analyser need the definition linked into rtatool too, not
    # only into the snapshot tool. Same reasoning as SpecimenComponent.cpp
    # appearing in both target_sources lists (comment above
    # rtatool_snapshot): one static library would not receive the
    # JUCE_MODULE_AVAILABLE_* defines juce_add_gui_app injects.
    src/measure/Analyser.cpp
    # PhaseUnwrap.cpp is what TransferView.cpp's unwrap toggle (decision 4)
    # calls into -- JUCE-free, but the same one-static-library caveat above
    # applies, so it is named explicitly in both GUI binaries' source lists.
    src/measure/PhaseUnwrap.cpp
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
    src/view/TransferRibbon.cpp
    # MtwLayer.cpp is the MTW-specific per-band alpha and seam drawing (task 7
    # of the MTW plan), needed here for the specimen render.
    src/view/MtwLayer.cpp
    # MtwReadout.cpp is the per-band integration-seconds strip (record §5,
    # station-4 fix pass finding 1), needed here for the same reason.
    src/view/MtwReadout.cpp
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
    src/measure/SyntheticSnapshot.cpp
    src/measure/PhaseUnwrap.cpp
    # main-live.png additionally renders the real MainComponent in synthetic
    # mode (T10/T11's own composition root, plan §0 item 4's "no hardware at
    # all, on CI, and offscreen" made literal) -- which needs the live-device
    # path's JUCE-owning sources too, and AudioIo along with it. This is the
    # one deviation from plan §3.6's "must not link juce_audio_devices": that
    # line was written before this tool also had to construct MainComponent
    # itself, not only feed a StaticSnapshotSource into RtaView.
    src/MainComponent.cpp
    src/view/DevicePanel.cpp
    src/view/ChannelRoleTable.cpp
    src/measure/AnalysisThread.cpp
    src/measure/SyntheticInput.cpp
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
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
