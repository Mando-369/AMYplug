// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "FxProcessor.h"
#include "FxEditor.h"
#include <cmath>

namespace amyplug
{
using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout FxProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    // Bitcrusher: crushed sample rate + bit depth. Ranges/defaults match the
    // instrument's master-FX so a patch sounds identical here (16 bit + 48 kHz = bypass).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::freq, 1 }, "Freq",
        NormalisableRange<float> { 200.0f, 48000.0f, 1.0f, 0.25f }, 48000.0f));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { fxid::bits, 1 }, "Bit", 2, 16, 16));

    // WDF diode saturator: drive is gain-compensated THD (0 dB = unity character).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::drive, 1 }, "Drive",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    // Insert controls (FX plugin only).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::mix, 1 }, "Mix",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::output, 1 }, "Output",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    return layout;
}

FxProcessor::FxProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AMYPLUGFX", createLayout())
{
    pBits   = state.getRawParameterValue(fxid::bits);
    pFreq   = state.getRawParameterValue(fxid::freq);
    pDrive  = state.getRawParameterValue(fxid::drive);
    pMix    = state.getRawParameterValue(fxid::mix);
    pOutput = state.getRawParameterValue(fxid::output);

    // Non-compensated drive: like running a synth's output volume into the clipper —
    // Drive pushes level into the diodes and out, clipping hotter/louder (the "Synth
    // Vol into the clipper" character), instead of the level-steady Kalos compensation.
    clip.setGainCompensation(false);
}

void FxProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 1.0 ? sampleRate : 48000.0;
    crush.prepare(sr);
    clip.prepare(sr);
    dryBuf.setSize(2, samplesPerBlock, false, false, true);
    outLin = (float) std::pow(10.0, (pOutput ? pOutput->load() : 0.0f) / 20.0);
}

bool FxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void FxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = buffer.getNumChannels();
    const int n   = buffer.getNumSamples();

    const float mix = pMix ? pMix->load() : 1.0f;

    // Keep the dry signal only when the mix control needs it (pre-sized, no alloc).
    const bool needDry = mix < 0.999f;
    if (needDry)
    {
        if (dryBuf.getNumChannels() < nch || dryBuf.getNumSamples() < n)
            dryBuf.setSize(nch, n, false, false, true);   // grow if the host lied about block size
        for (int ch = 0; ch < nch; ++ch)
            dryBuf.copyFrom(ch, 0, buffer, ch, 0, n);
    }

    // Bitcrusher -> diode saturator, exactly the instrument's master chain.
    if (pFreq)  crush.setFreqHz(pFreq->load());
    if (pBits)  crush.setBits(pBits->load());
    if (pDrive) clip.setDriveDb(pDrive->load());
    clip.setOutputDb(0.0f);   // ceiling at 0 dBFS (doubles as a safety limiter)

    auto* chans = buffer.getArrayOfWritePointers();
    crush.process(chans, nch, n);
    clip.process(chans, nch, n);

    // Dry/wet blend, then output makeup as a click-free per-block linear ramp
    // from last block's gain to this block's target (standard zipper-free gain).
    const float startG = outLin;
    const float endG   = (float) std::pow(10.0, (pOutput ? pOutput->load() : 0.0f) / 20.0);
    const float dG     = n > 0 ? (endG - startG) / (float) n : 0.0f;
    for (int ch = 0; ch < nch; ++ch)
    {
        auto* w = buffer.getWritePointer(ch);
        const auto* d = needDry ? dryBuf.getReadPointer(ch) : nullptr;
        float g = startG;
        for (int i = 0; i < n; ++i)
        {
            const float mixed = needDry ? (d[i] * (1.0f - mix) + w[i] * mix) : w[i];
            w[i] = mixed * g;
            g += dG;
        }
    }
    outLin = endG;
}

juce::AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor(*this); }

void FxProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void FxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace amyplug

// The plugin entry point JUCE's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new amyplug::FxProcessor();
}
