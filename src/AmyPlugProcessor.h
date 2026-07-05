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
#include <mutex>
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
    void         requestPanic();     // flushes all notes (RT flag + immediate HW send)

    // User-patch (preset) save/load, used by the editor's browser.
    void saveUserPatch(const juce::String& name);
    bool loadUserPatch(const juce::String& name);
    bool loadUserPatch(const juce::String& group, const juce::String& name);
    // Import a DX7 .syx cartridge: every voice becomes a named FM user patch.
    // Returns the number of voices imported (0 if the file isn't a DX7 dump).
    int  importDx7Cartridge(const juce::File& file);
    // Decode a built-in DX7/ALGO preset into the FM editor as an editable patch.
    // Returns false if the patch isn't FM (e.g. a Juno analog preset).
    bool loadFactoryPatchIntoEditor(int patchNumber);
    void applyPreset(const PatchModel& preset);   // push preset values to the params
    void         setMode(IAmyBackend::Kind mode);                   // Software/Hardware
    IAmyBackend::Kind currentMode() const { return activeKind; }
    // Single-owner arbitration for AMY's global engine. In Software mode a second
    // instance can't render until it owns the engine; the editor shows a banner +
    // a "take over" button when this instance is the odd one out. See EngineOwnership.h.
    bool         ownsSoftwareEngine() const;
    void         takeOverSoftwareEngine();                          // user hand-over
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
    // Serializes every access to `model` (a std::vector-backed struct). It is touched
    // only OFF the audio thread — the message thread (handleAsyncUpdate, editor) and
    // the host thread (get/setStateInformation) both sync/rebuild from it, and
    // pluginval's "Parameter thread safety" test drives those concurrently. The audio
    // thread reads param atomics (never `model`), so this lock is RT-safe. Recursive
    // because the model-touching methods nest (setState -> rebuild -> syncModel).
    std::recursive_mutex modelMutex;
    PatchLibrary patchLib;
    NoteRouter   router;

    std::unique_ptr<IAmyBackend> software;
    std::unique_ptr<IAmyBackend> hardware;
    IAmyBackend*      active     = nullptr;
    IAmyBackend::Kind activeKind = IAmyBackend::Kind::Software;
    double lastSampleRate = 44100.0;   // remembered so setMode can (re)prepare software
    int    lastBlockSize  = 512;
    bool   wasSoftwareOwner = false;   // audio-thread edge detect: silent -> owning

    std::atomic<bool> panicRequested { false };
    // Set on the message thread by rebuildEngineFromModel; consumed on the audio
    // thread to flush the NoteRouter. A rebuild resets AMY's voices, so the tracker
    // must be cleared too or held notes desync (phantom mono/legato notes). Routed
    // through a flag because NoteRouter is not safe to touch off the audio thread.
    std::atomic<bool> routerFlushRequested { false };

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
    std::atomic<float>* pMode      = nullptr;    // 0 = Software, 1 = Hardware

    // Analog-engine continuous params (streamed osc-level on change). vcfFreq/reso
    // reuse mCutoff/mReso; the amp envelope reuses mAttack..mRelease.
    Macro mVcfKbd, mVcfEnv, mLfoFreq, mLfoPitch, mLfoPwm, mLfoFilter;
    Macro mOscADuty, mOscALevel, mOscBDuty, mOscBLevel, mOscAFreq, mOscBFreq;
    Macro mOscCDuty, mOscCLevel, mOscDDuty, mOscDLevel, mOscCFreq, mOscDFreq;
    Macro mOscACoarse, mOscAFine, mOscBCoarse, mOscBFine;   // OSC A/B pitch offset
    Macro mOscCCoarse, mOscCFine, mOscDCoarse, mOscDFine;   // OSC C/D pitch offset
    Macro mGlide;                                           // portamento (i<ch>m), all engines
    Macro mUnisonDetune;                                    // unison spread (cents)
    std::atomic<float>* pUnisonVoices = nullptr;            // stacked analog osc copies
    // Analog LFO mode (Poly/Free/Key/Sync). Behavioural, not structural: the mode
    // only changes WHEN we phase-reset the LFO (P0) and, in Sync, WHAT freq we
    // stream — all handled live in streamAnalogParams (no rebuild). These are
    // audio-thread-only; the atomics carry the param values, the ints/doubles are
    // last-seen state for change detection.
    std::atomic<float>* pLfoMode     = nullptr;
    std::atomic<float>* pLfoSyncRate = nullptr;
    int    lastLfoMode  = -1;                    // detect mode-enter (phase-lock/retrigger)
    int    lastSyncRate = -1;
    double lastSyncHz   = -1.0;                  // last streamed Sync freq (Hz; -1 = none)
    double curBpm       = 120.0;                 // host tempo, refreshed each block
    bool   analogNoteOn = false;                 // a note-on landed this block (Key mode)
    std::atomic<bool> lfoResyncPending { true }; // a rebuild happened → re-assert Sync freq
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
    Macro mFmPitchRate[4], mFmPitchLevel[4];   // global pitch EG (ALGO osc bp0)
    // DX7-native operator freq: Coarse/Fine/Detune are structural (they change the
    // emitted ratio or fixed Hz -> rebuild); Output Level streams as the amp coef.
    std::atomic<float>* pFmCoarse[PatchModel::kFmOps] = {};
    std::atomic<float>* pFmFine[PatchModel::kFmOps]   = {};
    std::atomic<float>* pFmDetune[PatchModel::kFmOps] = {};
    float mFmFreqLast[PatchModel::kFmOps] { };   // last streamed ratio/Hz per op (change detect)
    // Key Velocity Sensitivity 0..7. Streamed live (not baked): velocity scales the
    // operator LEVEL at note-on (DX7 KVS). fmNoteVel = the latest note-on velocity that
    // the operator amps are scaled by; broadcast across voices, so it's per-note-on
    // (monophonic-exact; polyphonic uses the most recent hit).
    std::atomic<float>* pFmVel[PatchModel::kFmOps] = {};
    float mFmVelLast[PatchModel::kFmOps] { };    // last streamed velSens per op (change detect)
    std::atomic<float> fmNoteVel { 1.0f };       // latest MIDI note-on velocity (0..1)
    float mFmNoteVelLast = 1.0f;                  // last velocity the op amps were scaled by
    Macro mFmOutLevel[PatchModel::kFmOps];
    Macro mFmEgRate[PatchModel::kFmOps][4], mFmEgLevel[PatchModel::kFmOps][4];  // DX7 4R/4L EG
    std::atomic<float>* pFmFixed[PatchModel::kFmOps]   = {};    // fixed-frequency mode (structural)
    std::atomic<float>* pFmAms[PatchModel::kFmOps]     = {};    // amp mod sensitivity (structural)
    // LFO (all structural — a change re-emits the LFO wiring on the async rebuild).
    std::atomic<float>* pFmLfoSpeed = nullptr;
    std::atomic<float>* pFmLfoWave  = nullptr;
    std::atomic<float>* pFmLfoPmd   = nullptr;
    std::atomic<float>* pFmLfoAmd   = nullptr;
    std::atomic<float>* pFmLfoPms   = nullptr;
    std::atomic<float>* pFmTranspose = nullptr;

    static constexpr int kMacroSynth = 1;        // M2: macros target synth 1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugProcessor)
};
} // namespace amyplug
