// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "engine/SoftwareBackend.h"
#include "engine/HardwareBackend.h"
#include "engine/AmyWire.h"
#include <cstdio>
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
}

void AmyPlugProcessor::streamMacrosToBackend()
{
    if (active == nullptr) return;
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
            char bp[48];
            std::snprintf(bp, sizeof bp, "%d,1,%d,%.3f,%d,0",
                          (int) std::lround(a * 1000.0f), (int) std::lround(d * 1000.0f),
                          (double) s, (int) std::lround(r * 1000.0f));
            WireBuilder w; w.synth(ch).bp0(bp);
            active->streamWire(w.str(), w.size());
        }
    }
}

void AmyPlugProcessor::handleAsyncUpdate()
{
    rebuildEngineFromModel();   // message thread: structural change (patch/voices)
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
