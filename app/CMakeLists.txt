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

include(cmake/rtatool_sources.cmake)
target_sources(rtatool PRIVATE ${RTATOOL_SOURCES})

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

# tools/orphan_check.py's linker-reachability mode (process/orphan-check-
# linkmap): OFF by default, so it never touches the normal build's flags.
# When ON, rtatool alone (not JUCE, not rta_core/rta_platform/az_ui -- the
# call sites this tool cares about are only ever inside rtatool's own TUs or
# reached through them) is compiled and linked so that a function the app's
# entry point cannot reach is PROVABLY absent from the final image, not just
# "no grep match for its spelling":
#   /Ob0       disable inlining -- an inlined callee leaves no call-site
#              relocation for the linker to see, so it would look identical
#              to a genuinely dead function; every candidate must survive as
#              its own out-of-line symbol for /OPT:REF to have anything to
#              decide about.
#   /Gy        function-level linking (COMDATs) -- /OPT:REF can only discard
#              a function that the linker sees as its own separately
#              addressable unit; without /Gy, MSVC may still emit an entire
#              translation unit's code as one blob that keeps a dead
#              function reachable in the map for a real reason (a live
#              sibling in the same blob), a false negative.
#   /OPT:REF   discard COMDATs nothing references, transitively from the
#              entry point -- this is the reachability decision itself.
#   /OPT:NOICF explicitly disable Identical COMDAT Folding. /OPT:REF turns
#              ICF on by default in Release; ICF merges two functions with
#              byte-identical generated code under ONE symbol in the map
#              (usually the first alphabetically), which would silently
#              erase the SECOND function's name from the map even though it
#              is reachable -- exactly the kind of false orphan report this
#              tool exists to avoid.
#   /MAP       emit rtatool.map (Publics by Value + Static symbols), the
#              file orphan_check.py reads to decide liveness.
#
# "Every candidate must survive as its own out-of-line symbol" is the goal
# these flags aim for, not an absolute guarantee -- two liveness caveats this
# build cannot remove (fix round 1, verifier, F7; both documented in full in
# orphan_check.py's own module docstring, since they are properties of the
# OPTIMISED BINARY, not of these flags):
#   - a never-called virtual override of an instantiated class still reads
#     live through the vtable itself, which the linker sees as a reference
#     regardless of whether anything actually dispatches to that override;
#   - the compiler can still delete a call whose callee is side-effect-free
#     and whose result is discarded, which can make that callee's only call
#     site vanish and the callee then read as unreachable even though the
#     source genuinely calls it.
# Both bias toward FALSE NEGATIVES (calling something live that a human would
# call dead), never the reverse -- they can hide a real orphan, never invent
# one that is not.
option(RTA_ORPHAN_LINKMAP
    "Build rtatool for linker-reachability analysis (tools/orphan_check.py); MSVC only, off by default"
    OFF)
if(RTA_ORPHAN_LINKMAP)
    if(NOT MSVC)
        message(FATAL_ERROR
            "RTA_ORPHAN_LINKMAP uses MSVC-specific flags "
            "(/Ob0 /Gy /OPT:REF /OPT:NOICF /MAP) and only makes sense with "
            "the MSVC toolchain this repo builds with on Windows.")
    endif()
    target_compile_options(rtatool PRIVATE /Ob0 /Gy)
    target_link_options(rtatool PRIVATE /OPT:REF /OPT:NOICF /MAP)
endif()

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

include(cmake/rtatool_snapshot_sources.cmake)
target_sources(rtatool_snapshot PRIVATE ${RTATOOL_SNAPSHOT_SOURCES})

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
