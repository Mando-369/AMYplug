# cmake/amy.cmake
# Build AMY's C sources into a static library ("libamy") configured for *embedding*
# inside a host (no self-managed audio or MIDI I/O). The plugin drives rendering.
#
# AMY is a submodule at third_party/amy. We never edit it; we only select sources
# and set compile definitions.
#
# IMPORTANT (first task for Claude Code): after bootstrap.sh, inspect
#   third_party/amy/src/*.c  and  third_party/amy/Makefile
# to confirm the exact source list and the names of the configuration macros below
# (AMY_SAMPLE_RATE / AMY_BLOCK_SIZE may be set in a header instead of via -D).
# Grep for: AMY_BLOCK_SIZE, AMY_SAMPLE_RATE, AMY_NCHANS, output_sample_type.

set(AMY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amy")

if(NOT EXISTS "${AMY_DIR}/src")
    message(FATAL_ERROR
        "AMY submodule not found at ${AMY_DIR}. Run ./scripts/bootstrap.sh first.")
endif()

# Glob the portable C engine sources. We deliberately EXCLUDE platform back-ends
# (miniaudio / I2S / libminiaudio / examples) — we feed AMY no device I/O.
file(GLOB AMY_SOURCES CONFIGURE_DEPENDS "${AMY_DIR}/src/*.c")

# Filter out files we must not compile into a plugin (audio/midi device drivers,
# example mains, etc.). Adjust this list once the real file names are confirmed.
set(AMY_EXCLUDE_PATTERNS
    "miniaudio"      # AMY's desktop audio device backend
    "libminiaudio"
    "amy-example"    # example main()
    "amy-message"    # CLI tool with its own main()
    "examples")
foreach(src ${AMY_SOURCES})
    foreach(pat ${AMY_EXCLUDE_PATTERNS})
        if(src MATCHES "${pat}")
            list(REMOVE_ITEM AMY_SOURCES "${src}")
        endif()
    endforeach()
endforeach()

add_library(libamy STATIC ${AMY_SOURCES})

target_include_directories(libamy PUBLIC "${AMY_DIR}/src")

# Configure AMY for embedded/library use. Confirm/extend these once headers are read.
target_compile_definitions(libamy PUBLIC
    AMY_DAW_EMBED=1          # our own marker, harmless if unused
    # AMY_SAMPLE_RATE=44100  # uncomment/set if AMY exposes it as a -D macro
    # AMY_BLOCK_SIZE=256
)

# AMY is C; keep warnings sane but don't fail the build on its style.
if(NOT MSVC)
    target_compile_options(libamy PRIVATE -Wno-unused-function -Wno-unused-variable)
endif()

set_target_properties(libamy PROPERTIES POSITION_INDEPENDENT_CODE ON)
