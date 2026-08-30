// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// § 2 of docs/TEST_PROTOCOL.md — the offline half of the release stress pass, driving the
// REAL AmyPlugProcessor rather than a mock, so the resampler, the FX chain and AMY's own
// block buffering are all in the path.
//
// Hidden behind the "[.stress]" tag: `ctest` does not pick these up (Catch2 omits hidden
// tests from --list-tests), so CI stays fast. Run them deliberately:
//
//     amyplug_ui_tests "[stress]" --order decl
//
// The sanitizer rows of § 2 (RTSan / TSan / ASan / UBSan) are separate builds and are not
// here — they instrument the same binaries rather than adding cases.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AmyPlugProcessor.h"
#include "state/Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <memory>
#include <vector>
#include <cstdio>
#include <unistd.h>

namespace
{
using Proc = amyplug::AmyPlugProcessor;

// ⚠️ The engine rebuild is deferred: prepareToPlay and every structural parameter change
// call triggerAsyncUpdate(), and handleAsyncUpdate() does the actual work on the MESSAGE
// thread. A console harness has a MessageManager but nothing pumping it, so without this
// the patch is never loaded and the plugin renders pure silence — which passes a
// "stays finite" assertion perfectly while proving nothing at all.
void pump(int ms = 150)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(ms);
}

// ⚠️ The FIRST processBlock after this instance claims the global engine deliberately
// returns a silent block and CLEARS the incoming MIDI (AmyPlugProcessor::processBlock —
// "Just acquired it: ... emit one silent block"), because AMY may still hold the previous
// owner's state and the rebuild has to land first. So a harness that sends its note-on in
// block one has that note swallowed and measures silence. Prime the instance instead: a
// couple of empty blocks, then let the deferred rebuild run.
std::unique_ptr<Proc> makeProcessor(double sampleRate, int blockSize)
{
    auto p = std::make_unique<Proc>();
    p->setPlayConfigDetails(0, 2, sampleRate, blockSize);
    p->prepareToPlay(sampleRate, blockSize);
    pump();
    for (int i = 0; i < 2; ++i)
    {
        juce::AudioBuffer<float> warm(2, blockSize); warm.clear();
        juce::MidiBuffer none;
        p->processBlock(warm, none);
    }
    pump();
    return p;
}

juce::MidiBuffer noteOn(int note, int sample = 0)
{
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), sample);
    return m;
}

// Render `frames` total through `proc` in chunks of `chunk`, appending to `out`.
// A chunk of 0 is a legal call a host can make and must be a no-op, not a crash.
void render(Proc& proc, int frames, int chunk, std::vector<float>* out = nullptr)
{
    juce::AudioBuffer<float> buf(2, juce::jmax(1, chunk));
    for (int done = 0; done < frames; done += juce::jmax(1, chunk))
    {
        const int n = juce::jmin(chunk, frames - done);
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, juce::jmax(0, n));
        view.clear();
        juce::MidiBuffer empty;
        proc.processBlock(view, empty);
        if (out != nullptr)
            for (int i = 0; i < n; ++i) out->push_back(view.getSample(0, i));
    }
}

bool allFinite(const std::vector<float>& v)
{
    for (float s : v) if (! std::isfinite(s)) return false;
    return true;
}

double rms(const std::vector<float>& v, int from = 0)
{
    double acc = 0.0; int n = 0;
    for (std::size_t i = (std::size_t) from; i < v.size(); ++i) { acc += (double) v[i] * v[i]; ++n; }
    return n > 0 ? std::sqrt(acc / n) : 0.0;
}

// Upward zero crossings -> fundamental. Adequate for "is it the same pitch at every rate".
double fundamental(const std::vector<float>& v, double sampleRate)
{
    long crossings = 0; double prev = 0.0;
    for (float s : v) { if (prev <= 0.0 && s > 0.0) ++crossings; prev = s; }
    return v.empty() ? 0.0 : (double) crossings * sampleRate / (double) v.size();
}

void setParam(Proc& p, const char* id, float value)
{
    if (auto* param = p.apvts().getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

// A structural change (engine, patch, voice count) needs the deferred rebuild to land
// before the next render, or you measure the previous patch.
void setStructuralParam(Proc& p, const char* id, float value) { setParam(p, id, value); pump(); }
} // namespace

// ---------------------------------------------------------------------------
// § 2 · edge block sizes
// ---------------------------------------------------------------------------
// AMY renders in fixed AMY_BLOCK_SIZE chunks internally, so the host's block size and
// AMY's never line up. That seam is where a partial-block bug lives, and it is silent:
// the plugin still makes sound, it just makes slightly *different* sound depending on
// how the host happened to carve the buffer. Rendering the same material at several
// chunk sizes and comparing sample-for-sample is the assertion that catches it.
TEST_CASE("stress: odd and extreme block sizes render identically", "[.stress]")
{
    constexpr double kSR     = 48000.0;
    constexpr int    kFrames = 8192;

    // ⚠️ AMY is ONE GLOBAL engine, and its state (FX buffers, timebase) survives a
    // processor being destroyed. The very first instance in a process therefore renders
    // from genuinely zeroed buffers and every later one does not — so a reference taken
    // first disagrees with all the chunk variants by an identical amount, which looks
    // exactly like a block-size bug and is not one. Burn a throwaway instance so the
    // reference and the variants are all measured from the same warm state. (A hermetic
    // version of this test needs a process per chunk size.)
    { auto warmup = makeProcessor(kSR, 512); render(*warmup, 4096, 512); }

    // Every instance is PREPARED at the same block size and only the chunk actually handed
    // to processBlock varies — prepareToPlay derives fxSmoothCoef from samplesPerBlock, so
    // preparing them differently changes the FX ramp and is a second variable.
    constexpr int kPrepared = 4096;

    std::vector<float> reference;
    {
        auto p = makeProcessor(kSR, kPrepared);
        juce::AudioBuffer<float> b(2, 512); b.clear();
        auto m = noteOn(60);
        p->processBlock(b, m);
        for (int i = 0; i < 512; ++i) reference.push_back(b.getSample(0, i));
        render(*p, kFrames - 512, 512, &reference);
    }
    REQUIRE(reference.size() == (std::size_t) kFrames);
    REQUIRE(allFinite(reference));
    INFO("the reference render is silent — the rest of this case would prove nothing");
    REQUIRE(rms(reference) > 1.0e-5);

    for (int chunk : { 1, 7, 13, 64, 4096 })
    {
        std::vector<float> got;
        auto p = makeProcessor(kSR, kPrepared);
        juce::AudioBuffer<float> b(2, chunk); b.clear();
        auto m = noteOn(60);
        p->processBlock(b, m);
        for (int i = 0; i < chunk; ++i) got.push_back(b.getSample(0, i));
        render(*p, kFrames - chunk, chunk, &got);

        INFO("chunk size " << chunk);
        REQUIRE(got.size() == (std::size_t) kFrames);
        CHECK(allFinite(got));

        double worst = 0.0; int worstAt = -1;
        for (int i = 0; i < kFrames; ++i)
        {
            const double d = std::fabs((double) got[(std::size_t) i] - reference[(std::size_t) i]);
            if (d > worst) { worst = d; worstAt = i; }
        }
        INFO("largest divergence " << worst << " at frame " << worstAt);
        CHECK(worst < 1.0e-6);
    }
}

TEST_CASE("stress: a zero-length block is a no-op, not a crash", "[.stress]")
{
    auto p = makeProcessor(48000.0, 512);
    juce::AudioBuffer<float> empty(2, 0);
    juce::MidiBuffer midi;
    CHECK_NOTHROW(p->processBlock(empty, midi));
    auto on = noteOn(60);
    CHECK_NOTHROW(p->processBlock(empty, on));      // a note arriving on an empty block
    std::vector<float> after;
    render(*p, 2048, 256, &after);
    CHECK(allFinite(after));
}

// ---------------------------------------------------------------------------
// § 2 · sample rates
// ---------------------------------------------------------------------------
// AMY's own sample rate is a compile-time constant, so every host rate other than that one
// goes through our resampler (docs/ENGINE_NOTES.md). The thing that must hold across all of
// them is PITCH: a note must sound at the same frequency whatever the host is running at.
TEST_CASE("stress: pitch holds across every host sample rate", "[.stress]")
{
    std::vector<double> measured;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        auto p = makeProcessor(sr, 512);
        setStructuralParam(*p, amyplug::params::id::engine, 1.0f);   // Analog: a plain osc
        juce::AudioBuffer<float> b(2, 512); b.clear();
        auto m = noteOn(69);                                    // A4 = 440 Hz
        p->processBlock(b, m);
        std::vector<float> audio;
        render(*p, (int) (sr * 0.5), 512, &audio);              // half a second, past the attack

        INFO("sample rate " << sr);
        CHECK(allFinite(audio));
        CHECK(rms(audio) > 1.0e-5);
        const double hz = fundamental(audio, sr);
        INFO("fundamental measured at " << hz << " Hz");
        measured.push_back(hz);
    }
    // Compare rates against each other rather than against a nominal: the point is that the
    // resampler does not transpose, not that a saw's zero-crossing count equals its f0.
    const double first = measured.front();
    REQUIRE(first > 1.0);
    for (double hz : measured)
    {
        INFO("all rates must agree; got " << hz << " vs " << first);
        CHECK(std::fabs(hz - first) / first < 0.02);
    }
}

TEST_CASE("stress: a mid-stream sample-rate change does not break the engine", "[.stress]")
{
    auto p = makeProcessor(48000.0, 512);
    juce::AudioBuffer<float> b(2, 512); b.clear();
    auto m = noteOn(60);
    p->processBlock(b, m);
    render(*p, 4096, 512);

    for (double sr : { 96000.0, 44100.0, 192000.0 })
    {
        p->releaseResources();
        p->setPlayConfigDetails(0, 2, sr, 512);
        p->prepareToPlay(sr, 512);
        pump();
        std::vector<float> audio;
        juce::AudioBuffer<float> nb(2, 512); nb.clear();
        auto on = noteOn(60);
        p->processBlock(nb, on);
        for (int i = 0; i < 512; ++i) audio.push_back(nb.getSample(0, i));
        render(*p, 4096, 512, &audio);
        INFO("after switching to " << sr);
        CHECK(allFinite(audio));
        CHECK(rms(audio) > 1.0e-5);
    }
}

// ---------------------------------------------------------------------------
// § 2 · NaN / denormal soak
// ---------------------------------------------------------------------------
// Every feedback path open, driven hard, for long enough that a denormal or a runaway
// coefficient would have surfaced. A NaN in a reverb tail never recovers — one bad sample
// poisons the buffer for the rest of the session, so the assertion is simply: still finite.
TEST_CASE("stress: full FX feedback soak stays finite", "[.stress]")
{
    auto p = makeProcessor(48000.0, 256);
    setParam(*p, amyplug::params::id::reverb,       1.0f);
    setParam(*p, amyplug::params::id::reverbSize,   1.0f);
    setParam(*p, amyplug::params::id::reverbDamping,0.0f);
    setParam(*p, amyplug::params::id::echo,         1.0f);
    setParam(*p, amyplug::params::id::echoFeedback, 0.95f);
    setParam(*p, amyplug::params::id::echoTime,     40.0f);
    setParam(*p, amyplug::params::id::chorus,       1.0f);
    setParam(*p, amyplug::params::id::clipDrive,    24.0f);
    setParam(*p, amyplug::params::id::masterVolume, 10.0f);
    pump();

    std::vector<float> audio;
    int held = -1;
    // ~20 s of dense notes at full velocity.
    for (int block = 0; block < 3800; ++block)
    {
        juce::AudioBuffer<float> b(2, 256); b.clear();
        juce::MidiBuffer m;
        if (block % 8 == 0) { held = 36 + (block / 8) % 40;
                              m.addEvent(juce::MidiMessage::noteOn (1, held, 1.0f), 0); }
        if (block % 8 == 5 && held >= 0)
                            { m.addEvent(juce::MidiMessage::noteOff(1, held), 0); held = -1; }
        p->processBlock(b, m);
        if (block % 25 == 0)
            for (int i = 0; i < 256; ++i) audio.push_back(b.getSample(0, i));
    }
    INFO("a non-finite sample appeared during the soak");
    CHECK(allFinite(audio));

    // ...and it must still be a signal, not a silent survivor.
    CHECK(rms(audio) > 1.0e-6);

    // Release everything and let the tails run out: the engine must settle, not sustain.
    // Release everything and let the tails run out. Echo at 0.95 feedback / 40 ms needs
    // ~5 s to reach -60 dB, so compare a LATE window against the loud passage rather than
    // against the first few milliseconds of the tail — those are still ringing up.
    const double loud = rms(audio);
    p->requestPanic();
    std::vector<float> tail;
    render(*p, 48000 * 12, 256, &tail);
    CHECK(allFinite(tail));
    const double settled = rms(std::vector<float>(tail.end() - 48000, tail.end()));
    INFO("loud passage rms " << loud << ", final second rms " << settled);
    // NOT "must decay": reverb liveness 1.0 with damping 0.0 is deliberately a near-infinite
    // resonator, so sustaining is the setting working as asked. What must never happen is
    // GROWTH — a feedback path that gains on itself ends in a NaN or a blown speaker.
    CHECK(settled <= loud * 1.25);
}

// ---------------------------------------------------------------------------
// § 2 · automation stress
// ---------------------------------------------------------------------------
// A host writing automation as fast as the block rate. This is where a coefficient swap
// without smoothing shows up as a burst, and where a parameter read on the audio thread
// races the message thread that wrote it.
TEST_CASE("stress: block-rate automation on every continuous parameter", "[.stress]")
{
    auto p = makeProcessor(48000.0, 128);
    setParam(*p, amyplug::params::id::reverb, 0.5f);
    setParam(*p, amyplug::params::id::echo,   0.4f);

    struct Sweep { const char* id; float lo, hi; };
    const Sweep sweeps[] = {
        { amyplug::params::id::filterCutoff,  20.0f,  18000.0f },
        { amyplug::params::id::filterReso,     0.0f,      1.0f },
        { amyplug::params::id::reverbSize,     0.0f,      1.0f },
        { amyplug::params::id::echoFeedback,   0.0f,      0.9f },
        { amyplug::params::id::chorusRate,     0.1f,      8.0f },
        { amyplug::params::id::clipDrive,      0.0f,     24.0f },
        { amyplug::params::id::bcBits,         2.0f,     16.0f },
        { amyplug::params::id::outputGain,   -24.0f,      6.0f },
    };

    juce::AudioBuffer<float> b(2, 128); b.clear();
    auto on = noteOn(60);
    p->processBlock(b, on);

    std::vector<float> audio;
    const int blocks = 4000;                      // ~10 s at 128 frames
    for (int i = 0; i < blocks; ++i)
    {
        const float t = 0.5f * (1.0f - std::cos((float) i * 0.05f));   // 0..1..0, fast
        for (const auto& s : sweeps) setParam(*p, s.id, s.lo + t * (s.hi - s.lo));

        juce::AudioBuffer<float> blk(2, 128); blk.clear();
        juce::MidiBuffer m;
        if (i % 200 == 0)   m.addEvent(juce::MidiMessage::noteOn (1, 48 + (i / 200) % 24, 1.0f), 0);
        if (i % 200 == 150) m.addEvent(juce::MidiMessage::noteOff(1, 48 + (i / 200) % 24), 0);
        p->processBlock(blk, m);
        for (int n = 0; n < 128; ++n) audio.push_back(blk.getSample(0, n));
    }

    CHECK(allFinite(audio));
    // No sample may exceed a sane ceiling: a coefficient swap that blows up shows here long
    // before it shows in a spectrum.
    float peak = 0.0f;
    for (float s : audio) peak = juce::jmax(peak, std::fabs(s));
    INFO("peak during the sweep was " << peak);
    CHECK(peak < 8.0f);
}

// ---------------------------------------------------------------------------
// § 2 · silence discipline (the synth's form of the bypass-null row)
// ---------------------------------------------------------------------------
// AMYplug exposes no bypass parameter — bypass is the host's. The equivalent assertion for
// an instrument is that it is genuinely silent when it should be, which is golden rule #1
// measured at the processor rather than in NoteRouter's bookkeeping.
TEST_CASE("stress: silent before the first note, and after panic", "[.stress]")
{
    auto p = makeProcessor(48000.0, 256);

    std::vector<float> idle;
    render(*p, 48000, 256, &idle);
    INFO("the plugin is making sound before any note arrives");
    CHECK(rms(idle) == Catch::Approx(0.0).margin(1.0e-9));

    juce::AudioBuffer<float> b(2, 256); b.clear();
    auto on = noteOn(60);
    p->processBlock(b, on);
    std::vector<float> sounding;
    render(*p, 24000, 256, &sounding);
    REQUIRE(rms(sounding) > 1.0e-5);              // it really was playing

    p->requestPanic();
    std::vector<float> settle;
    render(*p, 48000 * 4, 256, &settle);          // 4 s for every tail to run out
    std::vector<float> lastSecond(settle.end() - 48000, settle.end());
    INFO("still sounding 3 s after panic, rms " << rms(lastSecond));
    CHECK(rms(lastSecond) == Catch::Approx(0.0).margin(1.0e-7));
}


// ---------------------------------------------------------------------------
// § 2 · every engine sounds, and every engine goes silent
// ---------------------------------------------------------------------------
// Golden rule #1 measured at the PROCESSOR for all three engines. The engine-level
// tests drive libamy with hand-written wire strings, which is how an AMY change can
// break a fixture while the shipped signal path is fine (and vice versa). This drives
// what PatchModel actually emits.
TEST_CASE("stress: each engine sounds and then goes silent", "[.stress]")
{
    struct Case { int engine; const char* name; };
    for (auto c : { Case{0, "Factory"}, Case{1, "Analog"}, Case{2, "FM/DX7"} })
    {
        auto p = makeProcessor(48000.0, 256);
        setStructuralParam(*p, amyplug::params::id::engine, (float) c.engine);

        juce::AudioBuffer<float> b(2, 256); b.clear();
        auto on = noteOn(60);
        p->processBlock(b, on);
        std::vector<float> sounding;
        for (int i = 0; i < 256; ++i) sounding.push_back(b.getSample(0, i));
        render(*p, 24000, 256, &sounding);

        INFO(c.name << " engine produced no sound at all");
        CHECK(rms(sounding) > 1.0e-5);
        CHECK(allFinite(sounding));

        // Note-off, then a generous tail, then it must be silent.
        juce::AudioBuffer<float> ob(2, 256); ob.clear();
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        p->processBlock(ob, off);
        std::vector<float> tail;
        render(*p, 48000 * 4, 256, &tail);
        std::vector<float> last(tail.end() - 24000, tail.end());
        INFO(c.name << " engine still sounding 3.5 s after note-off, rms " << rms(last));
        CHECK(rms(last) == Catch::Approx(0.0).margin(1.0e-7));

        // ...and panic must also silence a held note.
        juce::AudioBuffer<float> hb(2, 256); hb.clear();
        auto on2 = noteOn(60);
        p->processBlock(hb, on2);
        render(*p, 12000, 256);
        p->requestPanic();
        std::vector<float> afterPanic;
        render(*p, 48000 * 4, 256, &afterPanic);
        std::vector<float> lastP(afterPanic.end() - 24000, afterPanic.end());
        INFO(c.name << " engine survived PANIC, rms " << rms(lastP));
        CHECK(rms(lastP) == Catch::Approx(0.0).margin(1.0e-7));
    }
}

// ---------------------------------------------------------------------------
// § 2 · switching engines must never address an oscillator the voice doesn't have
// ---------------------------------------------------------------------------
// streamAnalogParams/streamFmParams address oscillators BY INDEX. The engine parameter
// flips immediately; the rebuild that gives AMY the matching graph is deferred to the
// message thread and then queued, so without a gate there is a multi-block window where
// FM operator writes land on the PREVIOUS engine's (smaller) voice. AMY >= 1.2.162
// rejects those with "addressed osc N is outside this voice's M oscs"; older AMY accepted
// them and wrote out of range, which is why this is worth a test rather than a comment.
//
// Ground truth is AMY's own complaint on stderr, so the test captures it. On an AMY that
// predates the check this passes trivially — it is still the right assertion to encode.
TEST_CASE("stress: an engine switch never addresses an out-of-range oscillator", "[.stress]")
{
    const auto log = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("amyplug_oscrange_check.txt");
    log.deleteFile();

    std::fflush(stderr);
    const int savedStderr = dup(fileno(stderr));
    REQUIRE(savedStderr >= 0);
    REQUIRE(std::freopen(log.getFullPathName().toRawUTF8(), "w", stderr) != nullptr);

    // Cycle through every engine, both directions, with audio running throughout —
    // the switch is the moment under test, so do plenty of them.
    for (int pass = 0; pass < 2; ++pass)
        for (int engine : { 0, 1, 2, 1, 0, 2 })
        {
            auto p = makeProcessor(48000.0, 256);
            setStructuralParam(*p, amyplug::params::id::engine, (float) engine);
            juce::AudioBuffer<float> b(2, 256); b.clear();
            auto on = noteOn(60);
            p->processBlock(b, on);
            render(*p, 8000, 256);
            p->requestPanic();
            render(*p, 2000, 256);
        }

    std::fflush(stderr);
    dup2(savedStderr, fileno(stderr));
    close(savedStderr);

    const auto captured = log.loadFileAsString();
    const bool outOfRange = captured.contains("outside this voice");
    INFO("AMY reported an out-of-range oscillator write:\n" << captured);
    CHECK_FALSE(outOfRange);
    log.deleteFile();
}
