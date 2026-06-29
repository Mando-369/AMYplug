// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "engine/SoftwareBackend.h"
#include "engine/HardwareBackend.h"
#include "engine/AmyWire.h"
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
                      params::id::lfoWave, params::id::vcfType })
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
    mVcfA.ptr      = state.getRawParameterValue(params::id::vcfAttack);
    mVcfD.ptr      = state.getRawParameterValue(params::id::vcfDecay);
    mVcfS.ptr      = state.getRawParameterValue(params::id::vcfSustain);
    mVcfR.ptr      = state.getRawParameterValue(params::id::vcfRelease);
    mEqLow.ptr     = state.getRawParameterValue(params::id::eqLow);
    mEqMid.ptr     = state.getRawParameterValue(params::id::eqMid);
    mEqHigh.ptr    = state.getRawParameterValue(params::id::eqHigh);
}

bool AmyPlugProcessor::engineIsAnalog() const
{
    return pEngine != nullptr && pEngine->load(std::memory_order_relaxed) >= 0.5f;
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
    s.engine = engineIsAnalog() ? PatchModel::Engine::Analog : PatchModel::Engine::FactoryPreset;
    auto& a = s.analog;
    auto rd = [this] (const char* id, float def) {
        if (auto* p = state.getRawParameterValue(id)) return p->load(); return def; };
    a.aWave = (int) rd(params::id::oscAWave, 3);  a.bWave = (int) rd(params::id::oscBWave, 1);
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
}

void AmyPlugProcessor::streamMacrosToBackend()
{
    if (active == nullptr) return;
    if (engineIsAnalog()) { streamAnalogParams(); return; }
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
    stream(mVolume, [] (WireBuilder& w, float v) { w.volume(v); });
    stream(mReverb, [] (WireBuilder& w, float v) { w.reverb(v); });
    stream(mChorus, [] (WireBuilder& w, float v) { w.chorus(v); });
    stream(mEcho,   [] (WireBuilder& w, float v) { w.echo(v); });

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
    // OSC A/B duty (idx0) + level (amp const). Waves are structural (rebuild).
    one(mOscADuty, "i1v2d%g");  one(mOscALevel, "i1v2a%g");
    one(mOscBDuty, "i1v3d%g");  one(mOscBLevel, "i1v3a%g");

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

    // Global 3-band EQ (x low,mid,high) — re-send if any changed.
    if (mEqLow.ptr && mEqMid.ptr && mEqHigh.ptr)
    {
        const float l=mEqLow.ptr->load(), md=mEqMid.ptr->load(), h=mEqHigh.ptr->load();
        if (l!=mEqLow.last || md!=mEqMid.last || h!=mEqHigh.last)
        {
            mEqLow.last=l; mEqMid.last=md; mEqHigh.last=h;
            char b[48]; std::snprintf(b, sizeof b, "x%g,%g,%g", (double)l,(double)md,(double)h);
            active->streamWire(b, (int) std::strlen(b));
        }
    }
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
    PatchModel preset;
    if (! patchLib.load(name, preset))
        return false;
    applyPreset(preset);
    return true;
}

void AmyPlugProcessor::applyPreset(const PatchModel& preset)
{
    // Drive everything through the parameters so the host sees the change (UI,
    // automation, undo) and the existing rebuild/stream paths fire.
    auto setP = [this] (const char* id, float value)
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

    // Engine + analog (Juno) controls.
    setP(params::id::engine, s.engine == PatchModel::Engine::Analog ? 1.0f : 0.0f);
    const auto& a = s.analog;
    setP(params::id::oscAWave, (float) a.aWave);   setP(params::id::oscBWave, (float) a.bWave);
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
