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
    src/dev/SpecimenComponent.cpp
    src/AppTypefaces.cpp
)

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
    ${CMAKE_SOURCE_DIR}/tools/snapshot.cpp
    src/dev/SpecimenComponent.cpp
    src/AppTypefaces.cpp
)

target_include_directories(rtatool_snapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(rtatool_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(rtatool_snapshot PRIVATE
    RtaToolFonts
    az_ui
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
