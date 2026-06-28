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

target_sources(AMYplug PRIVATE
    src/AmyPlugProcessor.cpp
    src/AmyPlugEditor.cpp
    src/engine/SoftwareBackend.cpp
    src/engine/HardwareBackend.cpp
    src/engine/AmyWire.cpp
    src/engine/amy_platform_stubs.c   # no-op AMY device hooks for library mode
    src/midi/NoteRouter.cpp
    src/state/PatchModel.cpp)

target_include_directories(AMYplug PRIVATE src)

target_compile_definitions(AMYplug PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)   # allowed under AGPL/GPL builds

target_link_libraries(AMYplug PRIVATE
    libamy
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AMYPLUG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
