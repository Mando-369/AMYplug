// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Engine-level smoke test: drives the embedded AMY engine with the exact wire
// messages PatchModel + NoteRouter emit, and asserts it (a) makes sound and
// (b) goes silent after note-off — the project's #1 guarantee (no hanging notes).
// Links libamy directly; no JUCE, no audio device.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>

extern "C" {
#include "amy.h"
}

namespace
{
struct Stats { double rms = 0.0; double peak = 0.0; };

Stats renderStats(int blocks)
{
    double sumsq = 0.0, peak = 0.0; long count = 0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        const int16_t* b = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
        {
            const double s = (double) b[i] / 32768.0;
            sumsq += s * s; ++count;
            if (std::fabs(s) > peak) peak = std::fabs(s);
        }
    }
    return { std::sqrt(sumsq / (double) count), peak };
}
} // namespace

TEST_CASE("AMY renders audible sound for note 60 on Juno patch 0", "[engine]")
{
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi  = AMY_MIDI_IS_NONE;
    c.platform.multicore   = 0;
    c.platform.multithread = 0;
    amy_start(c);

    amy_add_message((char*) "i1iv6K0Z");   // synth 1, 6 voices, patch 0 (Juno)
    amy_add_message((char*) "i1n60l1Z");    // note-on: note 60, vel 1.0

    const Stats sound = renderStats(200);   // ~1.16 s
    REQUIRE(sound.rms  > 1e-4);
    REQUIRE(sound.peak > 1e-3);

    // Note-off must silence it (no hanging note).
    amy_add_message((char*) "i1n60l0Z");
    renderStats(400);                       // let the release envelope finish
    const Stats tail = renderStats(40);
    REQUIRE(tail.rms < 1e-4);

    amy_stop();
}

namespace
{
// High-frequency energy proxy on the LEFT channel only (consecutive samples): mean
// squared first difference ~ a 1-pole HPF. Opening the filter adds treble -> rises.
double hfEnergy(int blocks)
{
    double sum = 0.0; long n = 0; double prev = 0.0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        const int16_t* b = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; ++i)      // left channel = even indices
        {
            const double s = (double) b[2 * i] / 32768.0;
            const double d = s - prev; prev = s;
            sum += d * d; ++n;
        }
    }
    return sum / (double) n;
}
} // namespace

TEST_CASE("Filter-cutoff macro audibly changes timbre (automation proof)", "[engine]")
{
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0; c.platform.multithread = 0;
    amy_start(c);

    amy_add_message((char*) "i1iv6K0Z");
    amy_add_message((char*) "i1n60l1Z");
    renderStats(200);                           // reach the filter-envelope sustain

    amy_add_message((char*) "i1F300Z");         // cutoff low -> dark
    renderStats(50);
    const double dark = hfEnergy(100);

    amy_add_message((char*) "i1F14000Z");       // cutoff high -> bright
    renderStats(50);
    const double bright = hfEnergy(100);

    INFO("dark HF=" << dark << "  bright HF=" << bright);
    REQUIRE(bright > dark * 1.5);               // opening the filter adds treble
    amy_stop();
}

TEST_CASE("RESET_ALL_NOTES silences a held note (panic / transport-stop path)", "[engine]")
{
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0; c.platform.multithread = 0;
    amy_start(c);

    amy_add_message((char*) "i1iv6K0Z");
    amy_add_message((char*) "i1n60l1Z");
    REQUIRE(renderStats(100).rms > 1e-4);   // sounding

    amy_add_message((char*) "S131072Z");    // RESET_ALL_NOTES (== Reset::AllNotes)
    renderStats(400);                       // release tails finish
    REQUIRE(renderStats(40).rms < 1e-4);    // silent

    amy_stop();
}
