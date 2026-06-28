// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "engine/SoftwareBackend.h"
#include "engine/HardwareBackend.h"

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

    state.addParameterListener(params::id::mode, this);
    // TODO: listen to the other sound-affecting params and mark rebuildPending,
    // or apply lightweight changes directly via backend->sendWire from a timer.
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

    // Translate incoming MIDI into backend note/CC events (deterministic offs).
    router.process(midi, *active);

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
    // Off audio-thread. Sync APVTS -> model first (TODO), then push to backend.
    if (active) active->rebuildFrom(model);
}

void AmyPlugProcessor::parameterChanged(const juce::String& id, float newValue)
{
    if (id == params::id::mode)
        setMode(newValue < 0.5f ? IAmyBackend::Kind::Software : IAmyBackend::Kind::Hardware);
    // else: mark rebuildPending or stream a targeted wire message (M2+).
}

void AmyPlugProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // Persist BOTH the APVTS and the PatchModel so recall is complete.
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
