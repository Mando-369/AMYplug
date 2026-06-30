// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "engine/SoftwareBackend.h"
#include "engine/HardwareBackend.h"
#include "engine/AmyWire.h"
#include "state/Dx7Import.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace amyplug
{
AmyPlugProcessor::AmyPlugProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AMYPLUG", params::createLayout())
{
    software = std::make_unique<SoftwareBackend>();
    hardware = std::make_unique<HardwareBackend>();
    active     = software.get();
    activeKind = IAmyBackend::Kind::Software;

    cacheParamPointers();

    // Structural params (need a full synth rebuild) are listened to; continuous
    // macros are polled on the audio thread (streamMacrosToBackend) instead.
    state.addParameterListener(params::id::mode,      this);
    state.addParameterListener(params::id::patchA,    this);
    state.addParameterListener(params::id::numVoices, this);
    // Structural changes rebuild the osc graph (a wave/filter-type restructure is
    // unsafe to stream live — it can race the engine-switch rebuild and hit an
    // oscillator that isn't set up yet). Continuous params stream live.
    for (auto* id : { params::id::engine, params::id::oscAWave, params::id::oscBWave,
                      params::id::lfoWave, params::id::vcfType,
                      params::id::fmAlgorithm })   // FM: routing change → rebuild
        state.addParameterListener(id, this);
}

void AmyPlugProcessor::cacheParamPointers()
{
    mCutoff.ptr  = state.getRawParameterValue(params::id::filterCutoff);
    mReso.ptr    = state.getRawParameterValue(params::id::filterReso);
    mVolume.ptr  = state.getRawParameterValue(params::id::masterVolume);
    mReverb.ptr  = state.getRawParameterValue(params::id::reverb);
    mChorus.ptr  = state.getRawParameterValue(params::id::chorus);
    mEcho.ptr    = state.getRawParameterValue(params::id::echo);
    mAttack.ptr  = state.getRawParameterValue(params::id::ampAttack);
    mDecay.ptr   = state.getRawParameterValue(params::id::ampDecay);
    mSustain.ptr = state.getRawParameterValue(params::id::ampSustain);
    mRelease.ptr = state.getRawParameterValue(params::id::ampRelease);
    pBendRange   = state.getRawParameterValue(params::id::pitchBendRange);
    pPatch       = state.getRawParameterValue(params::id::patchA);
    pEngine      = state.getRawParameterValue(params::id::engine);

    mVcfKbd.ptr    = state.getRawParameterValue(params::id::vcfKbd);
    mVcfEnv.ptr    = state.getRawParameterValue(params::id::vcfEnv);
    mLfoFreq.ptr   = state.getRawParameterValue(params::id::lfoFreq);
    mLfoPitch.ptr  = state.getRawParameterValue(params::id::lfoToPitch);
    mLfoPwm.ptr    = state.getRawParameterValue(params::id::lfoToPwm);
    mLfoFilter.ptr = state.getRawParameterValue(params::id::lfoToFilter);
    mOscADuty.ptr  = state.getRawParameterValue(params::id::oscADuty);
    mOscALevel.ptr = state.getRawParameterValue(params::id::oscALevel);
    mOscBDuty.ptr  = state.getRawParameterValue(params::id::oscBDuty);
    mOscBLevel.ptr = state.getRawParameterValue(params::id::oscBLevel);
    mOscAFreq.ptr  = state.getRawParameterValue(params::id::oscAFreq);
    mOscBFreq.ptr  = state.getRawParameterValue(params::id::oscBFreq);
    mVcfA.ptr      = state.getRawParameterValue(params::id::vcfAttack);
    mVcfD.ptr      = state.getRawParameterValue(params::id::vcfDecay);
    mVcfS.ptr      = state.getRawParameterValue(params::id::vcfSustain);
    mVcfR.ptr      = state.getRawParameterValue(params::id::vcfRelease);
    mEqLow.ptr     = state.getRawParameterValue(params::id::eqLow);
    mEqMid.ptr     = state.getRawParameterValue(params::id::eqMid);
    mEqHigh.ptr    = state.getRawParameterValue(params::id::eqHigh);
    mReverbSize.ptr  = state.getRawParameterValue(params::id::reverbSize);
    mReverbDamp.ptr  = state.getRawParameterValue(params::id::reverbDamping);
    mChorusRate.ptr  = state.getRawParameterValue(params::id::chorusRate);
    mChorusDepth.ptr = state.getRawParameterValue(params::id::chorusDepth);
    mEchoTime.ptr    = state.getRawParameterValue(params::id::echoTime);
    mEchoFb.ptr      = state.getRawParameterValue(params::id::echoFeedback);
    mEchoTone.ptr    = state.getRawParameterValue(params::id::echoTone);

    pAlgorithm     = state.getRawParameterValue(params::id::fmAlgorithm);
    mFmFeedback.ptr = state.getRawParameterValue(params::id::fmFeedback);
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int op = i + 1;
        mFmRatio[i].ptr = state.getRawParameterValue(params::id::fmOp(op, "ratio"));
        mFmLevel[i].ptr = state.getRawParameterValue(params::id::fmOp(op, "level"));
        mFmA[i].ptr     = state.getRawParameterValue(params::id::fmOp(op, "attack"));
        mFmD[i].ptr     = state.getRawParameterValue(params::id::fmOp(op, "decay"));
        mFmS[i].ptr     = state.getRawParameterValue(params::id::fmOp(op, "sustain"));
        mFmR[i].ptr     = state.getRawParameterValue(params::id::fmOp(op, "release"));
    }
}

bool AmyPlugProcessor::engineIsAnalog() const
{
    return pEngine != nullptr
        && (int) std::lround(pEngine->load(std::memory_order_relaxed)) == 1;   // 1 = Analog
}

bool AmyPlugProcessor::engineIsFM() const
{
    return pEngine != nullptr
        && (int) std::lround(pEngine->load(std::memory_order_relaxed)) == 2;   // 2 = FM
}

AmyPlugProcessor::~AmyPlugProcessor() = default;

void AmyPlugProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    software->prepare(sampleRate, samplesPerBlock);
    hardware->prepare(sampleRate, samplesPerBlock);
    router.prepare();
    rebuildEngineFromModel();
}

void AmyPlugProcessor::releaseResources()
{
    software->release();
    hardware->release();
}

bool AmyPlugProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AmyPlugProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Honour a pending Panic from the UI before anything else.
    if (panicRequested.exchange(false))
        router.allNotesOff(active);

    // Transport-aware hang prevention: stop -> flush.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            router.updateTransport(pos->getIsPlaying(), *active);

    // Keep the pitch-bend range current, then translate incoming MIDI into backend
    // note/CC events (deterministic offs).
    if (pBendRange != nullptr) router.setPitchBendRangeSemitones(pBendRange->load());
    router.process(midi, *active);

    // Stream any changed automatable macros to AMY (RT-safe, no allocation).
    streamMacrosToBackend();

    // Render (Software) or emit silence (Hardware).
    active->processBlock(buffer);

    // Hardware mode: if routing through the host MIDI-out, merge queued messages.
    if (activeKind == IAmyBackend::Kind::Hardware)
        if (auto* hw = dynamic_cast<HardwareBackend*>(active))
            hw->collectHostMidi(midi, buffer.getNumSamples());
        else
            midi.clear();
    else
        midi.clear(); // don't pass notes through in Software mode
}

void AmyPlugProcessor::setMode(IAmyBackend::Kind mode)
{
    if (mode == activeKind) return;
    router.allNotesOff(active);                 // never leave a hung note across modes
    active     = (mode == IAmyBackend::Kind::Software) ? software.get() : hardware.get();
    activeKind = mode;
    rebuildEngineFromModel();                   // bring the new backend up to date
}

void AmyPlugProcessor::rebuildEngineFromModel()
{
    // Off audio-thread. Sync APVTS -> model, then project the model onto the backend.
    syncModelFromParams();
    if (active) active->rebuildFrom(model);
}

void AmyPlugProcessor::syncModelFromParams()
{
    if (model.synths.empty()) model.synths.push_back({});
    auto& s = model.synths[0];                       // M2: single synth (channel 1)
    if (auto* p = state.getRawParameterValue(params::id::patchA))    s.patchNumber = (int) p->load();
    if (auto* p = state.getRawParameterValue(params::id::numVoices)) s.numVoices   = (int) p->load();
    if (mCutoff.ptr)  s.filterCutoff   = mCutoff.ptr->load();
    if (mReso.ptr)    s.filterReso     = mReso.ptr->load();
    if (mAttack.ptr)  s.ampAttack      = mAttack.ptr->load();
    if (mDecay.ptr)   s.ampDecay       = mDecay.ptr->load();
    if (mSustain.ptr) s.ampSustain     = mSustain.ptr->load();
    if (mRelease.ptr) s.ampRelease     = mRelease.ptr->load();
    if (mVolume.ptr)  model.masterVolume = mVolume.ptr->load();
    if (mReverb.ptr)  model.reverb     = mReverb.ptr->load();
    if (mChorus.ptr)  model.chorus     = mChorus.ptr->load();
    if (mEcho.ptr)    model.echo       = mEcho.ptr->load();

    // Engine + analog (Juno) params. filterCutoff/reso and the amp ADSR above are
    // reused as VCF freq/reso and the amp envelope, so mirror them into AnalogParams.
    s.engine = engineIsFM()     ? PatchModel::Engine::FM
             : engineIsAnalog() ? PatchModel::Engine::Analog
                                : PatchModel::Engine::FactoryPreset;
    auto& a = s.analog;
    auto rd = [this] (const char* id, float def) {
        if (auto* p = state.getRawParameterValue(id)) return p->load(); return def; };
    a.aWave = (int) rd(params::id::oscAWave, 3);  a.bWave = (int) rd(params::id::oscBWave, 1);
    a.aFreq = rd(params::id::oscAFreq, 440.0f);  a.bFreq = rd(params::id::oscBFreq, 440.0f);
    a.vcfFreq = s.filterCutoff; a.vcfReso = s.filterReso;     // reused params
    a.aDuty = rd(params::id::oscADuty, 0.5f);  a.bDuty = rd(params::id::oscBDuty, 0.5f);
    a.aLevel = rd(params::id::oscALevel, 0.7f); a.bLevel = rd(params::id::oscBLevel, 0.5f);
    a.lfoWave = (int) rd(params::id::lfoWave, 4); a.lfoFreq = rd(params::id::lfoFreq, 4.0f);
    a.lfoToPitch = rd(params::id::lfoToPitch, 0.0f); a.lfoToPwm = rd(params::id::lfoToPwm, 0.0f);
    a.lfoToFilter = rd(params::id::lfoToFilter, 0.0f);
    a.filterType = (int) rd(params::id::vcfType, 1);
    a.vcfKbd = rd(params::id::vcfKbd, 0.0f); a.vcfEnv = rd(params::id::vcfEnv, 0.0f);
    a.vcfA = rd(params::id::vcfAttack, 0.0f); a.vcfD = rd(params::id::vcfDecay, 0.6f);
    a.vcfS = rd(params::id::vcfSustain, 0.3f); a.vcfR = rd(params::id::vcfRelease, 0.4f);
    a.ampA = s.ampAttack; a.ampD = s.ampDecay; a.ampS = s.ampSustain; a.ampR = s.ampRelease;
    if (mEqLow.ptr)  model.eqLow  = mEqLow.ptr->load();
    if (mEqMid.ptr)  model.eqMid  = mEqMid.ptr->load();
    if (mEqHigh.ptr) model.eqHigh = mEqHigh.ptr->load();
    model.reverbSize    = rd(params::id::reverbSize,    0.85f);
    model.reverbDamping = rd(params::id::reverbDamping, 0.5f);
    model.chorusRate    = rd(params::id::chorusRate,    0.5f);
    model.chorusDepth   = rd(params::id::chorusDepth,   0.5f);
    model.echoTime      = rd(params::id::echoTime,      500.0f);
    model.echoFeedback  = rd(params::id::echoFeedback,  0.0f);
    model.echoTone      = rd(params::id::echoTone,      0.0f);

    // FM (DX7) engine params. The master amp envelope reuses s.ampAttack..ampRelease.
    auto& fm = s.fm;
    if (pAlgorithm)      fm.algorithm = (int) std::lround(pAlgorithm->load()) + 1;   // choice index 0..31 -> algo 1..32
    if (mFmFeedback.ptr) fm.feedback  = mFmFeedback.ptr->load();
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        auto& op = fm.ops[i];
        if (mFmRatio[i].ptr) op.ratio = mFmRatio[i].ptr->load();
        if (mFmLevel[i].ptr) op.level = mFmLevel[i].ptr->load();
        if (mFmA[i].ptr)     op.a     = mFmA[i].ptr->load();
        if (mFmD[i].ptr)     op.d     = mFmD[i].ptr->load();
        if (mFmS[i].ptr)     op.s     = mFmS[i].ptr->load();
        if (mFmR[i].ptr)     op.r     = mFmR[i].ptr->load();
    }
}

void AmyPlugProcessor::streamMacrosToBackend()
{
    if (active == nullptr) return;
    if (engineIsAnalog()) { streamAnalogParams(); return; }
    if (engineIsFM())     { streamFmParams();     return; }
    constexpr int ch = kMacroSynth;

    auto stream = [this] (Macro& m, auto build)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last)
        {
            m.last = v;
            WireBuilder w; build(w, v);
            active->streamWire(w.str(), w.size());
        }
    };

    stream(mCutoff, [] (WireBuilder& w, float v) { w.synth(ch).filterFreq(v); });
    stream(mReso,   [] (WireBuilder& w, float v) { w.synth(ch).resonance(v); });
    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ

    // Amp ADSR -> one bp0 breakpoint message, rebuilt if any of the four changed.
    if (mAttack.ptr && mDecay.ptr && mSustain.ptr && mRelease.ptr)
    {
        const float a = mAttack.ptr->load(std::memory_order_relaxed);
        const float d = mDecay.ptr->load(std::memory_order_relaxed);
        const float s = mSustain.ptr->load(std::memory_order_relaxed);
        const float r = mRelease.ptr->load(std::memory_order_relaxed);
        if (a != mAttack.last || d != mDecay.last || s != mSustain.last || r != mRelease.last)
        {
            mAttack.last = a; mDecay.last = d; mSustain.last = s; mRelease.last = r;
            // bp0 is the amp envelope only on Juno (0..127); on DX7 etc. it drives
            // the carrier's pitch, so don't override it there (record the change but
            // don't send). Mirrors PatchModel::ampEnvIsBp0.
            const int curPatch = pPatch ? (int) std::lround(pPatch->load(std::memory_order_relaxed)) : 0;
            if (curPatch >= 0 && curPatch <= 127)
            {
                char bp[48];
                std::snprintf(bp, sizeof bp, "%d,1,%d,%.3f,%d,0",
                              (int) std::lround(a * 1000.0f), (int) std::lround(d * 1000.0f),
                              (double) s, (int) std::lround(r * 1000.0f));
                WireBuilder w; w.synth(ch).bp0(bp);
                active->streamWire(w.str(), w.size());
            }
        }
    }
}

void AmyPlugProcessor::streamAnalogParams()
{
    // Audio thread, RT-safe (snprintf into stack buffers). Waves/filter-type/engine
    // are structural (rebuild); everything here streams osc-level coef updates that
    // AMY applies per-index without retriggering. Synth 1 (kMacroSynth) for M3b.
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[64]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };

    // Streams the SAME value to two oscillators (LFO depth hits both OSC A and B).
    // Must update m.last only once, after both, or the second osc never gets it.
    auto two = [this] (Macro& m, const char* fa, const char* fb)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last)
        {
            m.last = v;
            char b[64];
            std::snprintf(b, sizeof b, fa, (double) v); active->streamWire(b, (int) std::strlen(b));
            std::snprintf(b, sizeof b, fb, (double) v); active->streamWire(b, (int) std::strlen(b));
        }
    };
    // VCF (osc 0): freq=idx0, reso, kbd=idx1, env=idx4, lfo->filter=idx5.
    one(mCutoff,    "i1v0F%g");
    one(mReso,      "i1v0R%g");
    one(mVcfKbd,    "i1v0F,%g");
    one(mVcfEnv,    "i1v0F,,,,%g");
    one(mLfoFilter, "i1v0F,,,,,%g");
    // LFO (osc 1) freq, and its depth onto OSC A/B pitch (idx5) and PWM (idx5).
    one(mLfoFreq,   "i1v1f%g,0");
    two(mLfoPitch,  "i1v2f,,,,,%g", "i1v3f,,,,,%g");
    two(mLfoPwm,    "i1v2d,,,,,%g", "i1v3d,,,,,%g");
    // OSC A/B freq (idx0 = Hz at A4, keeps note tracking) + duty + level. Waves are
    // structural (rebuild).
    one(mOscAFreq, "i1v2f%g");  one(mOscADuty, "i1v2d%g");  one(mOscALevel, "i1v2a%g");
    one(mOscBFreq, "i1v3f%g");  one(mOscBDuty, "i1v3d%g");  one(mOscBLevel, "i1v3a%g");

    // Envelopes — re-send the whole breakpoint set if any of its 4 changed.
    auto env = [this] (char letter, Macro& a, Macro& d, Macro& s, Macro& r)
    {
        if (! (a.ptr && d.ptr && s.ptr && r.ptr)) return;
        const float av=a.ptr->load(), dv=d.ptr->load(), sv=s.ptr->load(), rv=r.ptr->load();
        if (av!=a.last || dv!=d.last || sv!=s.last || rv!=r.last)
        {
            a.last=av; d.last=dv; s.last=sv; r.last=rv;
            char b[80]; std::snprintf(b, sizeof b, "i1v0%c%d,1,%d,%.3f,%d,0", letter,
                (int) std::lround(av*1000.0f), (int) std::lround(dv*1000.0f), (double) sv,
                (int) std::lround(rv*1000.0f));
            active->streamWire(b, (int) std::strlen(b));
        }
    };
    env('A', mAttack, mDecay, mSustain, mRelease);  // amp env (bp0)
    env('B', mVcfA,   mVcfD,  mVcfS,    mVcfR);      // filter env (bp1)

    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ
}

void AmyPlugProcessor::streamFmParams()
{
    // Audio thread, RT-safe. The FM (DX7) voice: osc 0 is the ALGO controller,
    // oscs 1..6 are operators. The algorithm (o) is structural (rebuild); feedback,
    // per-op ratio/level/envelope and the master envelope stream as coef updates.
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[64]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };

    // Re-send an A/D/S/R breakpoint set (letter 'A' = bp0) for one osc if it changed.
    auto env = [this] (int osc, Macro& a, Macro& d, Macro& s, Macro& r)
    {
        if (! (a.ptr && d.ptr && s.ptr && r.ptr)) return;
        const float av=a.ptr->load(), dv=d.ptr->load(), sv=s.ptr->load(), rv=r.ptr->load();
        if (av!=a.last || dv!=d.last || sv!=s.last || rv!=r.last)
        {
            a.last=av; d.last=dv; s.last=sv; r.last=rv;
            char b[80]; std::snprintf(b, sizeof b, "i1v%dA%d,1,%d,%.3f,%d,0", osc,
                (int) std::lround(av*1000.0f), (int) std::lround(dv*1000.0f), (double) sv,
                (int) std::lround(rv*1000.0f));
            active->streamWire(b, (int) std::strlen(b));
        }
    };

    one(mFmFeedback, "i1v0b%g");                       // ALGO osc self-feedback

    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int osc = i + 1;
        char rf[24], lf[44];
        std::snprintf(rf, sizeof rf, "i1v%dI%%g", osc);                // ratio -> i1v<osc>I%g
        std::snprintf(lf, sizeof lf, "i1v%da%%g,0,0,%%g,0,0", osc);    // level -> const + eg0 coef
        one(mFmRatio[i], rf);
        if (mFmLevel[i].ptr != nullptr)                                // level writes %g twice
        {
            const float v = mFmLevel[i].ptr->load(std::memory_order_relaxed);
            if (v != mFmLevel[i].last)
            {
                mFmLevel[i].last = v;
                char b[64]; std::snprintf(b, sizeof b, lf, (double) v, (double) v);
                active->streamWire(b, (int) std::strlen(b));
            }
        }
        env(osc, mFmA[i], mFmD[i], mFmS[i], mFmR[i]);             // per-op envelope (owns release)
    }

    // No master amp env on the ALGO osc — the operator envelopes own the voice.
    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ
}

void AmyPlugProcessor::streamGlobalFx()
{
    // Master volume + bus FX (reverb/chorus/echo) + 3-band EQ — global, shared by
    // every engine. Streamed RT-safe so the UI knobs are live in all modes.
    if (active == nullptr) return;
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[48]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };
    one(mVolume, "V%g");

    // Each effect re-sends its full AMY parameter list when any of its params change.
    // Buffer-size params we don't expose are pinned to AMY's defaults.
    auto group = [this] (std::initializer_list<Macro*> ms, auto build)
    {
        bool changed = false;
        for (auto* m : ms) if (m->ptr && m->ptr->load(std::memory_order_relaxed) != m->last) changed = true;
        if (! changed) return;
        for (auto* m : ms) if (m->ptr) m->last = m->ptr->load(std::memory_order_relaxed);
        char b[80]; build(b);
        active->streamWire(b, (int) std::strlen(b));
    };
    group({ &mReverb, &mReverbSize, &mReverbDamp }, [this] (char* b) {
        std::snprintf(b, 80, "h%g,%g,%g,3000", (double) mReverb.last, (double) mReverbSize.last, (double) mReverbDamp.last); });
    group({ &mChorus, &mChorusRate, &mChorusDepth }, [this] (char* b) {
        std::snprintf(b, 80, "k%g,320,%g,%g", (double) mChorus.last, (double) mChorusRate.last, (double) mChorusDepth.last); });
    group({ &mEcho, &mEchoTime, &mEchoFb, &mEchoTone }, [this] (char* b) {
        std::snprintf(b, 80, "M%g,%g,743,%g,%g", (double) mEcho.last, (double) mEchoTime.last, (double) mEchoFb.last, (double) mEchoTone.last); });
    group({ &mEqLow, &mEqMid, &mEqHigh }, [this] (char* b) {
        std::snprintf(b, 80, "x%g,%g,%g", (double) mEqLow.last, (double) mEqMid.last, (double) mEqHigh.last); });
}

void AmyPlugProcessor::handleAsyncUpdate()
{
    rebuildEngineFromModel();   // message thread: structural change (patch/voices/engine)
}

void AmyPlugProcessor::saveUserPatch(const juce::String& name)
{
    syncModelFromParams();          // capture the live preset into the model
    patchLib.save(name, model);
}

bool AmyPlugProcessor::loadUserPatch(const juce::String& name)
{
    return loadUserPatch(juce::String(), name);
}

bool AmyPlugProcessor::loadUserPatch(const juce::String& group, const juce::String& name)
{
    PatchModel preset;
    if (! patchLib.load(group, name, preset))
        return false;
    applyPreset(preset);
    return true;
}

int AmyPlugProcessor::importDx7Cartridge(const juce::File& file)
{
    const auto voices = Dx7Import::parseFile(file);
    // Group the cartridge's voices under a folder named after the file, so they
    // don't clutter the user's own patch list.
    const juce::String group = file.getFileNameWithoutExtension();
    int imported = 0;
    juce::StringArray used;
    for (const auto& v : voices)
    {
        juce::String name = v.name.trim();
        if (name.isEmpty()) name = "DX7 Voice " + juce::String(imported + 1);
        // De-duplicate within the cartridge so each voice gets its own file.
        juce::String unique = name;
        for (int n = 2; used.contains(unique); ++n) unique = name + " " + juce::String(n);
        used.add(unique);

        patchLib.save(group, unique, Dx7Import::toPatchModel(v));
        ++imported;
    }
    return imported;
}

void AmyPlugProcessor::applyPreset(const PatchModel& preset)
{
    // Drive everything through the parameters so the host sees the change (UI,
    // automation, undo) and the existing rebuild/stream paths fire.
    auto setP = [this] (juce::StringRef id, float value)
    {
        if (auto* p = state.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    const PatchModel::Synth s = preset.synths.empty() ? PatchModel::Synth {} : preset.synths.front();
    setP(params::id::patchA,       (float) s.patchNumber);
    setP(params::id::numVoices,    (float) s.numVoices);
    setP(params::id::filterCutoff, s.filterCutoff);
    setP(params::id::filterReso,   s.filterReso);
    setP(params::id::ampAttack,    s.ampAttack);
    setP(params::id::ampDecay,     s.ampDecay);
    setP(params::id::ampSustain,   s.ampSustain);
    setP(params::id::ampRelease,   s.ampRelease);
    setP(params::id::masterVolume, preset.masterVolume);
    setP(params::id::reverb,       preset.reverb);
    setP(params::id::chorus,       preset.chorus);
    setP(params::id::echo,         preset.echo);

    // Engine + analog (Juno) controls. Engine choice index: 0 Factory, 1 Analog, 2 FM.
    setP(params::id::engine, s.engine == PatchModel::Engine::FM     ? 2.0f
                           : s.engine == PatchModel::Engine::Analog ? 1.0f : 0.0f);
    const auto& a = s.analog;
    setP(params::id::oscAWave, (float) a.aWave);   setP(params::id::oscBWave, (float) a.bWave);
    setP(params::id::oscAFreq, a.aFreq);           setP(params::id::oscBFreq, a.bFreq);
    setP(params::id::oscADuty, a.aDuty);           setP(params::id::oscBDuty, a.bDuty);
    setP(params::id::oscALevel, a.aLevel);         setP(params::id::oscBLevel, a.bLevel);
    setP(params::id::lfoWave, (float) a.lfoWave);  setP(params::id::lfoFreq, a.lfoFreq);
    setP(params::id::lfoToPitch, a.lfoToPitch);    setP(params::id::lfoToPwm, a.lfoToPwm);
    setP(params::id::lfoToFilter, a.lfoToFilter);
    setP(params::id::vcfType, (float) a.filterType);
    setP(params::id::vcfKbd, a.vcfKbd);            setP(params::id::vcfEnv, a.vcfEnv);
    setP(params::id::vcfAttack, a.vcfA);  setP(params::id::vcfDecay, a.vcfD);
    setP(params::id::vcfSustain, a.vcfS); setP(params::id::vcfRelease, a.vcfR);
    setP(params::id::eqLow, preset.eqLow); setP(params::id::eqMid, preset.eqMid);
    setP(params::id::eqHigh, preset.eqHigh);
    setP(params::id::reverbSize, preset.reverbSize);   setP(params::id::reverbDamping, preset.reverbDamping);
    setP(params::id::chorusRate, preset.chorusRate);   setP(params::id::chorusDepth, preset.chorusDepth);
    setP(params::id::echoTime, preset.echoTime);       setP(params::id::echoFeedback, preset.echoFeedback);
    setP(params::id::echoTone, preset.echoTone);

    // FM (DX7) controls.
    const auto& fm = s.fm;
    setP(params::id::fmAlgorithm, (float) (fm.algorithm - 1));   // algo 1..32 -> choice index 0..31
    setP(params::id::fmFeedback,  fm.feedback);
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int op = i + 1;
        const auto& o = fm.ops[i];
        setP(params::id::fmOp(op, "ratio"),   o.ratio);
        setP(params::id::fmOp(op, "level"),   o.level);
        setP(params::id::fmOp(op, "attack"),  o.a);
        setP(params::id::fmOp(op, "decay"),   o.d);
        setP(params::id::fmOp(op, "sustain"), o.s);
        setP(params::id::fmOp(op, "release"), o.r);
    }
}

void AmyPlugProcessor::parameterChanged(const juce::String& id, float newValue)
{
    if (id == params::id::mode)
        setMode(newValue < 0.5f ? IAmyBackend::Kind::Software : IAmyBackend::Kind::Hardware);
    else
        triggerAsyncUpdate();   // patchA / numVoices -> rebuild off the audio thread
}

void AmyPlugProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // Make the model reflect the live parameter values, then persist BOTH the
    // APVTS and the PatchModel so recall is complete.
    syncModelFromParams();
    juce::ValueTree root { "AMYplugState" };
    root.appendChild(state.copyState(), nullptr);
    root.appendChild(model.toValueTree(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, dest);
}

void AmyPlugProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto root = juce::ValueTree::fromXml(*xml);
        if (auto apvtsTree = root.getChildWithName(state.state.getType()); apvtsTree.isValid())
            state.replaceState(apvtsTree);
        if (auto patchTree = root.getChildWithName(PatchModel::kStateType); patchTree.isValid())
            model.fromValueTree(patchTree);
        rebuildEngineFromModel();   // recreate exact AMY state from the model
    }
}

juce::AudioProcessorEditor* AmyPlugProcessor::createEditor()
{
    return new AmyPlugEditor(*this);
}
} // namespace amyplug

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new amyplug::AmyPlugProcessor();
}
