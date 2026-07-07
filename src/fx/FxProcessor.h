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
#include "../amyfx/EnvelopeFollower.h"

namespace amyplug
{
// Shared param ids with the instrument where the control is identical (so a sound
// designer's muscle memory carries over); mix/output are FX-plugin-only.
namespace fxid
{
    // Filter (AMY VCF) — head of the chain.
    inline constexpr auto fltType   = "flt_type";     // 0 LP24, 1 LP12, 2 HP, 3 BP
    inline constexpr auto cutoff    = "flt_cutoff";   // Hz
    inline constexpr auto reso      = "flt_reso";     // Q
    inline constexpr auto envAmt    = "flt_env_amt";  // envelope-follower depth, octaves (+/-)
    inline constexpr auto follower  = "flt_follower"; // follower speed, 0 fast .. 1 slow

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
    BitCrusher crush;
    WdfClipper clip;

    std::atomic<float>* pFltType = nullptr;
    std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pReso = nullptr;
    std::atomic<float>* pEnvAmt = nullptr;
    std::atomic<float>* pFollower = nullptr;
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
