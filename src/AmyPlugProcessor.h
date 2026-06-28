// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/IAmyBackend.h"
#include "midi/NoteRouter.h"
#include "state/PatchModel.h"
#include "state/Parameters.h"
#include <memory>
#include <cmath>

namespace amyplug
{
class AmyPlugProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    AmyPlugProcessor();
    ~AmyPlugProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AMYplug"; }
    bool   acceptsMidi() const override  { return true; }
    bool   producesMidi() const override { return true; }  // Hardware mode
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- AMYplug-specific (used by the editor) ----------------------------
    juce::AudioProcessorValueTreeState& apvts() { return state; }
    PatchModel&  patch()        { return model; }
    void         requestPanic() { panicRequested.store(true); }     // RT-checked
    void         setMode(IAmyBackend::Kind mode);                   // Software/Hardware
    IAmyBackend::Kind currentMode() const { return activeKind; }
    IAmyBackend* backend() { return active; }                       // for device UI

private:
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;   // message thread: structural rebuild
    void rebuildEngineFromModel();       // off-thread; syncs backend to model+params
    void syncModelFromParams();          // copy current APVTS values into `model`
    void cacheParamPointers();           // resolve atomic param pointers once
    void streamMacrosToBackend();        // audio thread: push changed macros to AMY

    juce::AudioProcessorValueTreeState state;
    PatchModel  model;
    NoteRouter  router;

    std::unique_ptr<IAmyBackend> software;
    std::unique_ptr<IAmyBackend> hardware;
    IAmyBackend*      active     = nullptr;
    IAmyBackend::Kind activeKind = IAmyBackend::Kind::Software;

    std::atomic<bool> panicRequested { false };

    // Cached APVTS atomics for RT-safe macro streaming from processBlock. The
    // last* values are touched only on the audio thread (change detection).
    struct Macro { std::atomic<float>* ptr = nullptr; float last = std::nanf(""); };
    Macro mCutoff, mReso, mVolume, mReverb, mChorus, mEcho;
    Macro mAttack, mDecay, mSustain, mRelease;   // ADSR -> one bp0 message
    std::atomic<float>* pBendRange = nullptr;    // pitch-bend range (semitones)
    static constexpr int kMacroSynth = 1;        // M2: macros target synth 1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugProcessor)
};
} // namespace amyplug
