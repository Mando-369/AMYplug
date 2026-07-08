cmake_minimum_required(VERSION 3.22)

project(AMYplug VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(APPLE)
    # Build for both Intel + Apple Silicon by default. Override with -DCMAKE_OSX_ARCHITECTURES.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

option(AMYPLUG_BUILD_TESTS "Build AMYplug unit tests" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# JUCE: prefer an external checkout (AMYPLUG_JUCE_DIR) for local dev so we don't
# duplicate a multi-hundred-MB submodule on machines that already have JUCE; fall
# back to the third_party/JUCE submodule (used by CI / fresh clones). Set
# AMYPLUG_JUCE_DIR via CMakeUserPresets.json (gitignored) or -DAMYPLUG_JUCE_DIR=...
set(AMYPLUG_JUCE_DIR "" CACHE PATH
    "External JUCE source dir. If empty, uses the third_party/JUCE submodule.")

if(AMYPLUG_JUCE_DIR AND EXISTS "${AMYPLUG_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "AMYplug: using external JUCE at ${AMYPLUG_JUCE_DIR}")
    add_subdirectory("${AMYPLUG_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE-build")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
else()
    message(FATAL_ERROR
        "JUCE not found. Either set -DAMYPLUG_JUCE_DIR=/path/to/JUCE, or run "
        "./scripts/bootstrap.sh to fetch the third_party/JUCE submodule.")
endif()

# libamy: builds AMY's C sources into a static lib in 'library' mode (no audio/midi I/O).
include(cmake/amy.cmake)

# ---------------------------------------------------------------------------
# Generated: built-in patch names (parsed from AMY's patches.h comments — the
# same source AMY's web build uses). Names are stripped from the compiled library.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(AMYPLUG_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(AMYPLUG_PATCH_NAMES_H "${AMYPLUG_GENERATED_DIR}/BuiltinPatchNames.h")
add_custom_command(
    OUTPUT  "${AMYPLUG_PATCH_NAMES_H}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${AMYPLUG_GENERATED_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_patch_names.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amy/src/patches.h" "${AMYPLUG_PATCH_NAMES_H}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_patch_names.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amy/src/patches.h"
    COMMENT "Generating BuiltinPatchNames.h from AMY patches.h"
    VERBATIM)
add_custom_target(amyplug_patch_names DEPENDS "${AMYPLUG_PATCH_NAMES_H}")

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(AMYplug
    PRODUCT_NAME                "AMYplug"
    COMPANY_NAME                "Mando369"
    BUNDLE_ID                   "com.mando369.amyplug"
    PLUGIN_MANUFACTURER_CODE    Mand          # 4 chars, one uppercase
    PLUGIN_CODE                 Amyp          # 4 chars, one uppercase
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE          # for Hardware mode (SysEx to AMYboard)
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             "Instrument" "Synth"
    AU_MAIN_TYPE                "kAudioUnitType_MusicDevice")

juce_generate_juce_header(AMYplug)

# Bundled OFL-licensed fonts (assets/fonts) -> BinaryData for the custom LookAndFeel.
juce_add_binary_data(AmyPlugAssets SOURCES
    assets/fonts/BarlowSemiCondensed-ExtraBold.ttf
    assets/fonts/BarlowSemiCondensed-Bold.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/IBMPlexMono-Regular.ttf
    assets/fonts/DSEG7Classic-Regular.ttf)

target_sources(AMYplug PRIVATE
    src/AmyPlugProcessor.cpp
    src/AmyPlugEditor.cpp
    src/gui/AmyLookAndFeel.cpp
    src/gui/AmyFonts.cpp
    src/engine/SoftwareBackend.cpp
    src/engine/HardwareBackend.cpp
    src/engine/SerialPort.cpp
    src/engine/SerialTransport.cpp
    src/engine/FirmwareCheck.cpp
    src/engine/AmyWire.cpp
    src/engine/amy_platform_stubs.c   # no-op AMY device hooks for library mode
    src/midi/NoteRouter.cpp
    src/state/PatchModel.cpp
    src/state/PatchLibrary.cpp
    src/state/Dx7Import.cpp)

target_include_directories(AMYplug PRIVATE src "${AMYPLUG_GENERATED_DIR}")
add_dependencies(AMYplug amyplug_patch_names)

target_compile_definitions(AMYplug PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)   # allowed under AGPL/GPL builds

target_link_libraries(AMYplug PRIVATE
    libamy
    AmyPlugAssets
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# AMYplugFX — the output FX section as a standalone audio-effect plugin.
# Phase 1: the two host-side Faust effects (bitcrusher + WDF diode saturator),
# usable as an insert on any track (incl. the AMYboard's audio return). Shares
# the DSP + LookAndFeel + bundled fonts with the instrument; needs no AMY engine.
# ---------------------------------------------------------------------------
juce_add_plugin(AMYplugFX
    PRODUCT_NAME                "AMYplugFX"
    COMPANY_NAME                "Mando369"
    BUNDLE_ID                   "com.mando369.amyplugfx"
    PLUGIN_MANUFACTURER_CODE    Mand          # 4 chars, one uppercase
    PLUGIN_CODE                 Amfx          # unique from the instrument's Amyp
    FORMATS                     AU VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             "Fx" "Distortion"
    AU_MAIN_TYPE                "kAudioUnitType_Effect")

juce_generate_juce_header(AMYplugFX)

target_sources(AMYplugFX PRIVATE
    src/fx/FxProcessor.cpp
    src/fx/FxEditor.cpp
    src/gui/AmyLookAndFeel.cpp
    src/gui/AmyFonts.cpp)

target_include_directories(AMYplugFX PRIVATE src)

target_compile_definitions(AMYplugFX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AMYplugFX PRIVATE
    AmyPlugAssets
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless snapshot tool (UI dev): renders the editor to a PNG without a window.
# Off by default; enable with -DAMYPLUG_BUILD_SNAPSHOT=ON.
# ---------------------------------------------------------------------------
option(AMYPLUG_BUILD_SNAPSHOT "Build the headless editor snapshot tool" OFF)
if(AMYPLUG_BUILD_SNAPSHOT)
    juce_add_console_app(amyplug_snapshot PRODUCT_NAME "amyplug_snapshot")
    target_sources(amyplug_snapshot PRIVATE
        tools/Snapshot.cpp
        src/AmyPlugProcessor.cpp
        src/AmyPlugEditor.cpp
        src/gui/AmyLookAndFeel.cpp
        src/gui/AmyFonts.cpp
        src/engine/SoftwareBackend.cpp
        src/engine/HardwareBackend.cpp
        src/engine/SerialPort.cpp
        src/engine/SerialTransport.cpp
        src/engine/FirmwareCheck.cpp
        src/engine/AmyWire.cpp
        src/engine/amy_platform_stubs.c
        src/midi/NoteRouter.cpp
        src/state/PatchModel.cpp
        src/state/PatchLibrary.cpp
        src/state/Dx7Import.cpp)
    target_include_directories(amyplug_snapshot PRIVATE src "${AMYPLUG_GENERATED_DIR}")
    add_dependencies(amyplug_snapshot amyplug_patch_names)
    target_compile_definitions(amyplug_snapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(amyplug_snapshot PRIVATE
        libamy AmyPlugAssets
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    if(APPLE)
        add_custom_command(TARGET amyplug_snapshot POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_FILE:amyplug_snapshot>" VERBATIM)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AMYPLUG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
