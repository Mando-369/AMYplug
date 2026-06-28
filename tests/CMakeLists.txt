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

include(CTest)
include(Catch)
catch_discover_tests(amyplug_tests)
