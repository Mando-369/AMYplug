// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AMYplugFX — AMYplug's output FX section as a standalone audio-effect plugin.
// Phase 1: the two host-side Faust effects (bitcrusher + WDF diode saturator),
// the exact same DSP the instrument runs on its master bus, usable as an insert
// on any track — including the AMYboard's audio return in Hardware mode, where
// the instrument itself outputs silence. Phase 2 will add the Juno filter + AMY
// bus FX via AMY's external audio-input path.

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/BitCrusher.h"
#include "../dsp/WdfClipper.h"
#include "../amyfx/AmyFilter.h"
#include "../amyfx/AmyEq.h"
#include "../amyfx/AmyChorus.h"
#include "../amyfx/AmyEcho.h"
#include "../amyfx/AmyReverb.h"
#include "../amyfx/EnvelopeFollower.h"

namespace amyplug
{
// Shared param ids with the instrument where the control is identical (so a sound
// designer's muscle memory carries over); mix/output are FX-plugin-only.
namespace fxid
{
    // Per-effect bypass toggles (true = enabled).
    inline constexpr auto fltOn   = "flt_on";
    inline constexpr auto eqOn    = "eq_on";
    inline constexpr auto choOn   = "cho_on";
    inline constexpr auto echOn   = "ech_on";
    inline constexpr auto revOn   = "rev_on";
    inline constexpr auto crushOn = "crush_on";
    inline constexpr auto diodeOn = "diode_on";

    // Filter (AMY VCF) — head of the chain.
    inline constexpr auto fltType   = "flt_type";     // 0 LP24, 1 LP12, 2 HP, 3 BP
    inline constexpr auto cutoff    = "flt_cutoff";   // Hz
    inline constexpr auto reso      = "flt_reso";     // Q
    inline constexpr auto envAmt    = "flt_env_amt";  // envelope-follower depth, octaves (+/-)
    inline constexpr auto follower  = "flt_follower"; // follower speed, 0 slow .. 1 fast

    // 3-band bus EQ (AMY centers 800/2500/7000 Hz), gains in dB.
    inline constexpr auto eqLow  = "eq_low";
    inline constexpr auto eqMid  = "eq_mid";
    inline constexpr auto eqHigh = "eq_high";

    // Chorus (AMY triangle-LFO chorus).
    inline constexpr auto choMix   = "cho_mix";    // wet level
    inline constexpr auto choRate  = "cho_rate";   // LFO Hz
    inline constexpr auto choDepth = "cho_depth";  // mod depth

    // Echo (AMY fixed-delay echo).
    inline constexpr auto echMix  = "ech_mix";     // wet level
    inline constexpr auto echTime = "ech_time";    // delay ms
    inline constexpr auto echFb   = "ech_fb";      // feedback
    inline constexpr auto echTone = "ech_tone";    // tone (+LP / -HP)

    // Reverb (AMY stereo reverb).
    inline constexpr auto revMix  = "rev_mix";   // wet level
    inline constexpr auto revSize = "rev_size";  // liveness / decay
    inline constexpr auto revDamp = "rev_damp";  // HF damping

    inline constexpr auto bits   = "bc_bits";     // 2..16 (16 = transparent)
    inline constexpr auto freq   = "bc_freq";     // crushed sample rate, Hz
    inline constexpr auto drive  = "clip_drive";  // diode THD, dB (gain-compensated)
    inline constexpr auto mix    = "fx_mix";      // dry/wet, 0..1
    inline constexpr auto output = "fx_output";   // makeup / output, dB
}

class FxProcessor : public juce::AudioProcessor
{
public:
    FxProcessor();
    ~FxProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AMYplugFX"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return state; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState state;
    AmyFilter  filterL, filterR;   // AMY VCF, one per channel (state is per-channel)
    EnvelopeFollower follower;     // drives the cutoff from input level
    AmyEq      eqL, eqR;           // AMY 3-band bus EQ, one per channel
    AmyChorus  chorus;             // AMY triangle-LFO chorus (stereo)
    AmyEcho    echoL, echoR;       // AMY fixed-delay echo, one per channel
    AmyReverb  reverb;             // AMY stereo reverb (one instance, stereo)
    BitCrusher crush;
    WdfClipper clip;

    std::atomic<float>* pFltOn = nullptr;
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pChoOn = nullptr;
    std::atomic<float>* pEchOn = nullptr;
    std::atomic<float>* pRevOn = nullptr;
    std::atomic<float>* pCrushOn = nullptr;
    std::atomic<float>* pDiodeOn = nullptr;
    std::atomic<float>* pFltType = nullptr;
    std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pReso = nullptr;
    std::atomic<float>* pEnvAmt = nullptr;
    std::atomic<float>* pFollower = nullptr;
    std::atomic<float>* pEqLow = nullptr;
    std::atomic<float>* pEqMid = nullptr;
    std::atomic<float>* pEqHigh = nullptr;
    std::atomic<float>* pChoMix = nullptr;
    std::atomic<float>* pChoRate = nullptr;
    std::atomic<float>* pChoDepth = nullptr;
    std::atomic<float>* pEchMix = nullptr;
    std::atomic<float>* pEchTime = nullptr;
    std::atomic<float>* pEchFb = nullptr;
    std::atomic<float>* pEchTone = nullptr;
    std::atomic<float>* pRevMix = nullptr;
    std::atomic<float>* pRevSize = nullptr;
    std::atomic<float>* pRevDamp = nullptr;
    std::atomic<float>* pBits = nullptr;
    std::atomic<float>* pFreq = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    double sr = 48000.0;
    juce::AudioBuffer<float> dryBuf;   // pre-allocated dry copy for the mix control
    float outLin = 1.0f;               // output gain (linear), ramped per block

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxProcessor)
};
} // namespace amyplug
