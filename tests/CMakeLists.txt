# Unit tests via Catch2 (fetched by CMake). Pure logic only — no audio device,
# no JUCE GUI. We test the wire-message builder, note-router bookkeeping, and
# PatchModel round-trip serialization.

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4)
FetchContent_MakeAvailable(Catch2)

add_executable(amyplug_tests
    WireMessageTests.cpp
    WdfClipperTests.cpp
    BitCrusherTests.cpp
    EqTests.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AmyWire.cpp)

target_include_directories(amyplug_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# AmyWire.cpp includes AmyConfig.h only (no JUCE), so tests stay light. If a test
# needs JUCE types, link the relevant juce modules here.
target_link_libraries(amyplug_tests PRIVATE Catch2::Catch2WithMain)

# On Apple Silicon an unsigned Mach-O is SIGKILLed at launch. CMake/Xcode does not
# sign this plain console executable (only the plugin bundles get signed), and the
# universal-binary lipo step strips any ad-hoc signature, so catch_discover_tests
# (which runs the binary to enumerate cases) would fail. Ad-hoc re-sign post-build.
if(APPLE)
    add_custom_command(TARGET amyplug_tests POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:amyplug_tests>"
        VERBATIM)
endif()

# Engine render test: links libamy and proves the embedded engine makes sound and
# silences cleanly. Kept separate from the pure-logic tests above because it pulls
# in AMY's global engine state.
add_executable(amyplug_engine_tests
    EngineRenderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/amy_platform_stubs.c)
target_include_directories(amyplug_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(amyplug_engine_tests PRIVATE Catch2::Catch2WithMain libamy)
if(APPLE)
    add_custom_command(TARGET amyplug_engine_tests POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:amyplug_engine_tests>"
        VERBATIM)
endif()

# Logic tests that need JUCE types (MidiBuffer, ValueTree): NoteRouter lifecycle
# and PatchModel state round-trip. Built as a console app so JUCE's CMake wires up
# the module machinery; Catch2WithMain still provides main().
juce_add_console_app(amyplug_logic_tests)
target_sources(amyplug_logic_tests PRIVATE
    NoteRouterTests.cpp
    StateTests.cpp
    PatchLibraryTests.cpp
    PatchNamesTests.cpp
    PresetCatalogTests.cpp
    Dx7ImportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/midi/NoteRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/state/PatchModel.cpp
    ${CMAKE_SOURCE_DIR}/src/state/PatchLibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/state/Dx7Import.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AmyWire.cpp)
target_include_directories(amyplug_logic_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} "${AMYPLUG_GENERATED_DIR}")
add_dependencies(amyplug_logic_tests amyplug_patch_names)
target_compile_definitions(amyplug_logic_tests PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(amyplug_logic_tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_recommended_config_flags)
if(APPLE)
    add_custom_command(TARGET amyplug_logic_tests POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:amyplug_logic_tests>"
        VERBATIM)
endif()

# Tests that need the whole plugin — the real processor, libamy, the editor and the bundled
# fonts (the same source set the snapshot tool compiles). Two kinds live here:
#   EditorSizeTests  — the size-picker regression is in editor CONSTRUCTION (JUCE-UI-LnF__13
#                      §5), which no pure-logic test can reach. Runs in ctest.
#   PresetStateTests — loaded-preset identity + dirty mark; a user load moves every
#                      parameter through the real applyPreset. Runs in ctest.
#   StressTests      — § 2 of docs/TEST_PROTOCOL.md, tagged "[.stress]" so Catch2 hides them
#                      from --list-tests and ctest never picks them up. They take minutes.
#                      Run for a release:  amyplug_ui_tests "[stress]"
juce_add_console_app(amyplug_ui_tests)
target_sources(amyplug_ui_tests PRIVATE
    EditorSizeTests.cpp
    PresetStateTests.cpp
    TooltipTests.cpp
    StressTests.cpp
    ${CMAKE_SOURCE_DIR}/src/AmyPlugProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/AmyPlugEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/AmyLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/PresetsPage.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/AmyFonts.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/SoftwareBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/HardwareBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/SerialPort.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/SerialTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/FirmwareCheck.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AmyWire.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/amy_platform_stubs.c
    ${CMAKE_SOURCE_DIR}/src/midi/NoteRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/state/PatchModel.cpp
    ${CMAKE_SOURCE_DIR}/src/state/PatchLibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/state/Dx7Import.cpp)
target_include_directories(amyplug_ui_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src "${AMYPLUG_GENERATED_DIR}")
add_dependencies(amyplug_ui_tests amyplug_patch_names)
target_compile_definitions(amyplug_ui_tests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(amyplug_ui_tests PRIVATE
    Catch2::Catch2WithMain
    libamy AmyPlugAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)
if(APPLE)
    add_custom_command(TARGET amyplug_ui_tests POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:amyplug_ui_tests>"
        VERBATIM)
endif()

include(CTest)
include(Catch)
catch_discover_tests(amyplug_tests)
catch_discover_tests(amyplug_engine_tests)
catch_discover_tests(amyplug_logic_tests)
catch_discover_tests(amyplug_ui_tests)
