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

TEST_CASE("FM (ALGO) voice renders audible sound and silences on note-off", "[engine][fm]")
{
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0; c.platform.multithread = 0;
    amy_start(c);

    // Build a 6-operator ALGO voice the way PatchModel::emitFm does: operators on
    // oscs 1..6 (op1+op2 audible), then the ALGO controller on osc 0 (algorithm 1).
    amy_add_message((char*) "i1iv1in7Z");
    amy_add_message((char*) "i1v1w0a1,0,0,1,0,0I1A5,1,300,0.700,400,0Z");  // op1 carrier (level 1)
    amy_add_message((char*) "i1v2w0a1,0,0,1,0,0I2A5,1,500,0.300,400,0Z");  // op2 modulator (level 1)
    amy_add_message((char*) "i1v3w0a0,0,0,0,0,0I1A5,1,300,0.000,400,0Z");  // op3..6 silent (level 0)
    amy_add_message((char*) "i1v4w0a0,0,0,0,0,0I1A5,1,300,0.000,400,0Z");
    amy_add_message((char*) "i1v5w0a0,0,0,0,0,0I1A5,1,300,0.000,400,0Z");
    amy_add_message((char*) "i1v6w0a0,0,0,0,0,0I1A5,1,300,0.000,400,0Z");
    amy_add_message((char*) "i1v0w8f0,1a1,0,1,0,0,0b0.0000O6,5,4,3,2,1o1Z");
    amy_add_message((char*) "i1n60l1Z");                    // note-on

    const Stats sound = renderStats(200);
    REQUIRE(sound.rms  > 1e-4);
    REQUIRE(sound.peak > 1e-3);

    amy_add_message((char*) "i1n60l0Z");                    // note-off
    renderStats(400);
    REQUIRE(renderStats(40).rms < 1e-4);                    // silent (no hanging note)

    amy_stop();
}

TEST_CASE("FM operator release controls the note-off tail (regression)", "[engine][fm]")
{
    // The ALGO osc must NOT carry a master amp envelope, or it gates the voice off
    // before the operators release. With a constant-amp ALGO osc, the operator's own
    // release time governs the tail. Prove a long release still rings where a short
    // release has already gone silent.
    auto build = [] (const char* relMs)
    {
        amy_config_t c = amy_default_config();
        c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
        c.platform.multicore = 0; c.platform.multithread = 0;
        amy_start(c);
        amy_add_message((char*) "i1iv1in7Z");
        char b[160];
        std::snprintf(b, sizeof b, "i1v1w0a1,0,0,1,0,0I1A5,1,300,0.700,%s,0Z", relMs);
        amy_add_message(b);
        amy_add_message((char*) "i1v2w0a0,0,0,0,0,0I1Z");
        amy_add_message((char*) "i1v3w0a0,0,0,0,0,0I1Z");
        amy_add_message((char*) "i1v4w0a0,0,0,0,0,0I1Z");
        amy_add_message((char*) "i1v5w0a0,0,0,0,0,0I1Z");
        amy_add_message((char*) "i1v6w0a0,0,0,0,0,0I1Z");
        amy_add_message((char*) "i1v0w8f0,1a1,0,1,0,0,0b0O6,5,4,3,2,1o1Z");
        amy_add_message((char*) "i1n60l1Z");
    };

    build("50");                                   // short release
    renderStats(60);
    amy_add_message((char*) "i1n60l0Z");
    renderStats(40);                               // ~0.23 s after note-off
    const double shortTail = renderStats(20).rms;
    amy_stop();

    build("3000");                                 // long release
    renderStats(60);
    amy_add_message((char*) "i1n60l0Z");
    renderStats(40);                               // same point after note-off
    const double longTail = renderStats(20).rms;
    amy_stop();

    INFO("shortTail=" << shortTail << "  longTail=" << longTail);
    REQUIRE(shortTail < 1e-3);                      // short release: already silent
    REQUIRE(longTail  > 2e-3);                      // long release: still ringing (~40% of on-level)
}

TEST_CASE("Analog amp release controls the note-off tail (regression)", "[engine][analog]")
{
    // The amp env must live on the AUDIO oscs (osc2/osc3), not the silent VCA osc0,
    // or note-off cuts the voice dead regardless of the release knob. Build the voice
    // the way PatchModel::emitAnalog does and prove a long release still rings where
    // a short release has already gone silent.
    auto build = [] (const char* relMs)
    {
        amy_config_t c = amy_default_config();
        c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
        c.platform.multicore = 0; c.platform.multithread = 0;
        amy_start(c);
        amy_add_message((char*) "i1iv1in4Z");
        // osc0 filter-only (constant amp + filter env), osc1 LFO, osc2/3 audio with
        // the amp env (const+eg0 coef shaped by bp0).
        amy_add_message((char*) "i1v0w20G1F2500,0,,,0,0R0.7000a1,0,1,0,0,0B5,1,600,0.300,400,0c2L1Z");
        amy_add_message((char*) "i1v1w4f4.0000,0Z");
        char b[200];
        std::snprintf(b, sizeof b, "i1v2w3a0.7000,0,0,0.7000,0,0A5,1,100,0.700,%s,0d0.5f440c3L1Z", relMs);
        amy_add_message(b);
        std::snprintf(b, sizeof b, "i1v3w1a0.5000,0,0,0.5000,0,0A5,1,100,0.700,%s,0d0.5f440L1Z", relMs);
        amy_add_message(b);
        amy_add_message((char*) "i1n60l1Z");
    };

    build("50");                                   // short release
    renderStats(60);
    amy_add_message((char*) "i1n60l0Z");
    renderStats(40);                               // ~0.23 s after note-off
    const double shortTail = renderStats(20).rms;
    amy_stop();

    build("3000");                                 // long release
    renderStats(60);
    amy_add_message((char*) "i1n60l0Z");
    renderStats(40);                               // same point after note-off
    const double longTail = renderStats(20).rms;
    amy_stop();

    INFO("shortTail=" << shortTail << "  longTail=" << longTail);
    REQUIRE(shortTail < 1e-3);                      // short release: already silent
    REQUIRE(longTail  > 2e-3);                      // long release: still ringing
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

namespace
{
// Fundamental estimate via upward zero-crossings on the left channel — reliable for
// a plain sine. Returns Hz over the rendered window.
double fundamentalHz(int blocks)
{
    long crossings = 0, samples = 0; double prev = 0.0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        const int16_t* b = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; ++i)
        {
            const double s = (double) b[2 * i] / 32768.0;   // left channel
            if (prev <= 0.0 && s > 0.0) ++crossings;
            prev = s; ++samples;
        }
    }
    const double seconds = (double) samples / (double) AMY_SAMPLE_RATE;
    return seconds > 0.0 ? (double) crossings / seconds : 0.0;
}

double measureOscPitch(const char* oscSetup)
{
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE; c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0; c.platform.multithread = 0;
    amy_start(c);
    amy_add_message((char*) oscSetup);
    amy_add_message((char*) "v0n60l1Z");    // note 60 (C4 ~261.6 Hz)
    renderStats(20);                        // let it settle
    const double f = fundamentalHz(80);
    amy_stop();
    return f;
}
} // namespace

// Proves the emitFm ALGO fix: an EG0 (pitch-env) freq coef held at 1.0 raises the osc
// exactly one octave (combine_controls sums freq coefs in log2). Without it every FM
// voice played an octave below the built-in DX7 presets.
TEST_CASE("FM ALGO pitch: the EG0 pitch-env term raises the voice one octave", "[engine][fm]")
{
    const double base   = measureOscPitch("v0w0f0,1a1Z");                 // note tracking only
    const double raised = measureOscPitch("v0w0f0,1,0,1,0,0A0,1,0,1a1Z"); // + EG0 pitch env @1.0

    REQUIRE(base   > 235.0); REQUIRE(base   < 290.0);   // ~261.6 Hz
    REQUIRE(raised > 470.0); REQUIRE(raised < 580.0);   // ~523.3 Hz (one octave up)
    REQUIRE(raised > base * 1.8);
    REQUIRE(raised < base * 2.2);
}
