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

    // Per-effect bypass (default = enabled; defaults are transparent anyway).
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::fltOn,   1 }, "Filter On",   true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::eqOn,    1 }, "EQ On",       true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::choOn,   1 }, "Chorus On",   true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::revOn,   1 }, "Reverb On",   true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::crushOn, 1 }, "Bitcrush On", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { fxid::diodeOn, 1 }, "Diode On",    true));

    // Filter (AMY VCF) — head of the chain. Defaults = wide open (transparent).
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { fxid::fltType, 1 }, "Filter Type",
        StringArray { "LP 24", "LP 12", "HP", "BP" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::cutoff, 1 }, "Cutoff",
        NormalisableRange<float> { 20.0f, 20000.0f, 1.0f, 0.25f }, 20000.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::reso, 1 }, "Reso",
        NormalisableRange<float> { 0.5f, 16.0f, 0.01f, 0.5f }, 0.7f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::envAmt, 1 }, "Env Amt",
        NormalisableRange<float> { -6.0f, 6.0f, 0.01f }, 0.0f));      // octaves
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::follower, 1 }, "Env Speed",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));      // up = faster/snappier

    // 3-band bus EQ — dB gains, 0 = flat (matches the instrument's EQ range/centers).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::eqLow, 1 },  "EQ Low",  NormalisableRange<float> { -15.0f, 15.0f, 0.1f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::eqMid, 1 },  "EQ Mid",  NormalisableRange<float> { -15.0f, 15.0f, 0.1f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::eqHigh, 1 }, "EQ High", NormalisableRange<float> { -15.0f, 15.0f, 0.1f }, 0.0f));

    // Chorus — wet level (0 = off), LFO rate, depth.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::choMix,   1 }, "Chorus Mix",   NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::choRate,  1 }, "Chorus Rate",  NormalisableRange<float> { 0.05f, 8.0f, 0.01f, 0.4f }, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::choDepth, 1 }, "Chorus Depth", NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));

    // Reverb — wet level (0 = off), size (liveness/decay), damping.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::revMix,  1 }, "Reverb Mix",  NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::revSize, 1 }, "Reverb Size", NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.85f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { fxid::revDamp, 1 }, "Reverb Damp", NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));

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
    pFltOn    = state.getRawParameterValue(fxid::fltOn);
    pEqOn     = state.getRawParameterValue(fxid::eqOn);
    pChoOn    = state.getRawParameterValue(fxid::choOn);
    pRevOn    = state.getRawParameterValue(fxid::revOn);
    pCrushOn  = state.getRawParameterValue(fxid::crushOn);
    pDiodeOn  = state.getRawParameterValue(fxid::diodeOn);
    pFltType  = state.getRawParameterValue(fxid::fltType);
    pCutoff   = state.getRawParameterValue(fxid::cutoff);
    pReso     = state.getRawParameterValue(fxid::reso);
    pEnvAmt   = state.getRawParameterValue(fxid::envAmt);
    pFollower = state.getRawParameterValue(fxid::follower);
    pEqLow    = state.getRawParameterValue(fxid::eqLow);
    pEqMid    = state.getRawParameterValue(fxid::eqMid);
    pEqHigh   = state.getRawParameterValue(fxid::eqHigh);
    pChoMix   = state.getRawParameterValue(fxid::choMix);
    pChoRate  = state.getRawParameterValue(fxid::choRate);
    pChoDepth = state.getRawParameterValue(fxid::choDepth);
    pRevMix   = state.getRawParameterValue(fxid::revMix);
    pRevSize  = state.getRawParameterValue(fxid::revSize);
    pRevDamp  = state.getRawParameterValue(fxid::revDamp);
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
    filterL.prepare(sr); filterR.prepare(sr);
    follower.prepare(sr);
    eqL.prepare(sr); eqR.prepare(sr);
    chorus.prepare(sr);
    reverb.prepare(sr);
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

    // FILTER (head of chain) — the AMY VCF, cutoff opened by the envelope follower.
    // Chain order matches the synth's bus: filter -> (EQ/chorus/echo/reverb) -> crush -> diode.
    {
        const int   uiType   = pFltType ? (int) std::lround(pFltType->load()) : 0;
        const float cutParam = pCutoff ? pCutoff->load() : 20000.0f;
        const float reso     = pReso ? pReso->load() : 0.7f;
        const float envAmt   = pEnvAmt ? pEnvAmt->load() : 0.0f;
        const bool  isLP     = (uiType == 0 || uiType == 1);
        const bool  fltOn    = ! pFltOn || pFltOn->load() > 0.5f;
        // Bypassed, or a wide-open lowpass with no envelope (true transparent).
        const bool  open     = ! fltOn || (isLP && envAmt == 0.0f && cutParam >= 19999.0f);
        if (! open && nch > 0)
        {
            static constexpr int kAmyType[4] =
                { AmyFilter::LPF24, AmyFilter::LPF, AmyFilter::HPF, AmyFilter::BPF };
            const int type = kAmyType[juce::jlimit(0, 3, uiType)];
            // Env Speed: turn UP for faster/snappier tracking. 1 = ~0.5 ms attack /
            // ~20 ms release (percussive), 0 = ~15 ms / ~400 ms (smooth swells).
            const float spd = pFollower ? pFollower->load() : 0.5f;
            follower.setTimesMs(15.0f - spd * 14.5f, 400.0f - spd * 380.0f);
            float cut = cutParam;
            if (envAmt != 0.0f)
            {
                const float envPk = follower.processBlockPeak(buffer.getReadPointer(0), n);
                // Saturating sensitivity: a raw peak of ~0.2–0.4 (typical audio) should
                // already open the filter a lot. 1 - e^(-k·env) reaches ~0.7 at env=0.2,
                // ~0.9 at 0.4, and can't exceed 1 — musical and responsive, not sluggish.
                const float e = 1.0f - std::exp(-envPk * 6.0f);
                cut *= std::pow(2.0f, envAmt * e);
            }
            cut = juce::jlimit(20.0f, (float) (0.45 * sr), cut);
            filterL.process(buffer.getWritePointer(0), n, cut, reso, type);
            if (nch > 1) filterR.process(buffer.getWritePointer(1), n, cut, reso, type);
        }
    }

    // EQ — AMY's 3-band bus EQ (800/2500/7000). dB -> linear; skip when off or flat.
    {
        const bool  eqOn = ! pEqOn || pEqOn->load() > 0.5f;
        const float loDb = pEqLow  ? pEqLow->load()  : 0.0f;
        const float mdDb = pEqMid  ? pEqMid->load()  : 0.0f;
        const float hiDb = pEqHigh ? pEqHigh->load() : 0.0f;
        if (eqOn && (loDb != 0.0f || mdDb != 0.0f || hiDb != 0.0f))
        {
            const float gL = std::pow(10.0f, loDb / 20.0f);
            const float gM = std::pow(10.0f, mdDb / 20.0f);
            const float gH = std::pow(10.0f, hiDb / 20.0f);
            if (nch > 0) eqL.process(buffer.getWritePointer(0), n, gL, gM, gH);
            if (nch > 1) eqR.process(buffer.getWritePointer(1), n, gL, gM, gH);
        }
    }

    // CHORUS — AMY's triangle-LFO chorus (after EQ, before reverb).
    {
        const bool  choOn = ! pChoOn || pChoOn->load() > 0.5f;
        const float wet   = pChoMix ? pChoMix->load() : 0.0f;
        if (choOn && wet > 0.0001f && nch > 0)
        {
            chorus.setParams(pChoRate ? pChoRate->load() : 0.5f, pChoDepth ? pChoDepth->load() : 0.5f);
            chorus.processStereo(buffer.getWritePointer(0),
                                 nch > 1 ? buffer.getWritePointer(1) : nullptr, n, wet);
        }
    }

    // REVERB — AMY's stereo reverb (after chorus; echo will slot in ahead of it).
    {
        const bool  revOn = ! pRevOn || pRevOn->load() > 0.5f;
        const float wet   = pRevMix ? pRevMix->load() : 0.0f;
        if (revOn && wet > 0.0001f && nch > 0)
        {
            reverb.setParams(pRevSize ? pRevSize->load() : 0.85f,
                             3000.0f,
                             pRevDamp ? pRevDamp->load() : 0.5f);
            reverb.processStereo(buffer.getWritePointer(0),
                                 nch > 1 ? buffer.getWritePointer(1) : nullptr, n, wet);
        }
    }

    // Bitcrusher -> diode saturator, exactly the instrument's master chain.
    auto* chans = buffer.getArrayOfWritePointers();
    if (! pCrushOn || pCrushOn->load() > 0.5f)
    {
        if (pFreq)  crush.setFreqHz(pFreq->load());
        if (pBits)  crush.setBits(pBits->load());
        crush.process(chans, nch, n);
    }
    if (! pDiodeOn || pDiodeOn->load() > 0.5f)
    {
        if (pDrive) clip.setDriveDb(pDrive->load());
        clip.setOutputDb(0.0f);   // ceiling at 0 dBFS (doubles as a safety limiter)
        clip.process(chans, nch, n);
    }

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
