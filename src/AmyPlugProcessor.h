// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/IAmyBackend.h"
#include "dsp/BitCrusher.h"
#include "dsp/WdfClipper.h"
#include "midi/NoteRouter.h"
#include "state/PatchModel.h"
#include "state/PatchLibrary.h"
#include "state/Parameters.h"
#include <memory>
#include <cmath>

namespace amyplug
{
class HardwareBackend;

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
    PatchLibrary& patchLibrary() { return patchLib; }
    void         requestPanic() { panicRequested.store(true); }     // RT-checked

    // User-patch (preset) save/load, used by the editor's browser.
    void saveUserPatch(const juce::String& name);
    bool loadUserPatch(const juce::String& name);
    bool loadUserPatch(const juce::String& group, const juce::String& name);
    // Import a DX7 .syx cartridge: every voice becomes a named FM user patch.
    // Returns the number of voices imported (0 if the file isn't a DX7 dump).
    int  importDx7Cartridge(const juce::File& file);
    void applyPreset(const PatchModel& preset);   // push preset values to the params
    void         setMode(IAmyBackend::Kind mode);                   // Software/Hardware
    IAmyBackend::Kind currentMode() const { return activeKind; }
    IAmyBackend* backend() { return active; }                       // for device UI
    HardwareBackend* hardwareBackend();                             // AMYboard device config
    void         sendPatchToHardware();                             // push current patch as SysEx

private:
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;   // message thread: structural rebuild
    void rebuildEngineFromModel();       // off-thread; syncs backend to model+params
    void syncModelFromParams();          // copy current APVTS values into `model`
    void cacheParamPointers();           // resolve atomic param pointers once
    void streamMacrosToBackend();        // audio thread: push changed macros to AMY
    void streamAnalogParams();           // audio thread: analog (Juno) engine streaming
    void streamFmParams();               // audio thread: FM (DX7) engine streaming
    void streamGlobalFx();               // audio thread: master volume + reverb/chorus/echo/EQ (all engines)
    bool engineIsAnalog() const;
    bool engineIsFM() const;

    juce::AudioProcessorValueTreeState state;
    PatchModel   model;
    PatchLibrary patchLib;
    NoteRouter   router;

    std::unique_ptr<IAmyBackend> software;
    std::unique_ptr<IAmyBackend> hardware;
    IAmyBackend*      active     = nullptr;
    IAmyBackend::Kind activeKind = IAmyBackend::Kind::Software;

    std::atomic<bool> panicRequested { false };

    // Master output DSP — runs on the rendered buffer after the backend, before
    // the audio leaves the plugin: bitcrusher (retro grit) -> WDF diode saturator
    // (analog warmth). Both stream from params; the clipper's gain compensation is
    // internal. Software mode only (Hardware's buffer is silence).
    BitCrusher        crush;
    WdfClipper        clip;
    std::atomic<float>* pBcFreq    = nullptr;
    std::atomic<float>* pBcBits    = nullptr;
    std::atomic<float>* pClipDrive = nullptr;
    // True final output gain (JUCE-side), after the bitcrusher + saturator. AMY's
    // "Synth Vol" (master_volume -> V) is applied upstream inside the engine.
    std::atomic<float>* pOutputGain = nullptr;
    float               outGainCurrent = 1.0f;   // ramp origin (audio thread only)
    // Per-block one-pole coefficient (~25 ms) for smoothing streamed bus-FX values
    // so fast knob moves (Echo Time, EQ gains) don't crackle. Set in prepareToPlay.
    float               fxSmoothCoef = 1.0f;

    // Cached APVTS atomics for RT-safe macro streaming from processBlock. The
    // last* values are touched only on the audio thread (change detection).
    struct Macro { std::atomic<float>* ptr = nullptr; float last = std::nanf(""); };
    Macro mCutoff, mReso, mVolume, mReverb, mChorus, mEcho;
    Macro mAttack, mDecay, mSustain, mRelease;   // ADSR -> one bp0 message
    std::atomic<float>* pBendRange = nullptr;    // pitch-bend range (semitones)
    std::atomic<float>* pPatch     = nullptr;    // current patch number (for engine-aware macros)
    std::atomic<float>* pEngine    = nullptr;    // 0 = Factory, 1 = Analog
    std::atomic<float>* pVoiceMode = nullptr;    // 0 Poly, 1 Mono, 2 Legato

    // Analog-engine continuous params (streamed osc-level on change). vcfFreq/reso
    // reuse mCutoff/mReso; the amp envelope reuses mAttack..mRelease.
    Macro mVcfKbd, mVcfEnv, mLfoFreq, mLfoPitch, mLfoPwm, mLfoFilter;
    Macro mOscADuty, mOscALevel, mOscBDuty, mOscBLevel, mOscAFreq, mOscBFreq;
    Macro mOscACoarse, mOscAFine, mOscBCoarse, mOscBFine;   // OSC A/B pitch offset
    Macro mGlide;                                           // portamento (i<ch>m), all engines
    Macro mUnisonDetune;                                    // unison spread (cents)
    std::atomic<float>* pUnisonVoices = nullptr;            // stacked analog osc copies
    Macro mVcfA, mVcfD, mVcfS, mVcfR;            // VCF envelope (bp1)
    Macro mEqLow, mEqMid, mEqHigh;
    // Deeper effect params (streamed as full h/k/M lists in streamGlobalFx).
    Macro mReverbSize, mReverbDamp;
    Macro mChorusRate, mChorusDepth;
    Macro mEchoTime, mEchoFb, mEchoTone;

    // FM (DX7) engine continuous params. algorithm is structural (rebuild). The
    // master amp envelope reuses mAttack..mRelease. Per-op arrays index 0..5.
    std::atomic<float>* pAlgorithm = nullptr;
    Macro mFmFeedback;
    Macro mFmRatio[PatchModel::kFmOps], mFmLevel[PatchModel::kFmOps];
    Macro mFmA[PatchModel::kFmOps], mFmD[PatchModel::kFmOps];
    Macro mFmS[PatchModel::kFmOps], mFmR[PatchModel::kFmOps];

    static constexpr int kMacroSynth = 1;        // M2: macros target synth 1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugProcessor)
};
} // namespace amyplug
