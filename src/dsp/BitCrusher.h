// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// BitCrusher — retro sample-rate + bit-depth reduction, ported from the
// FAUST_TX81Z effect (Faust `ba.bitcrusher` by Julius O. Smith III and
// `ba.downSample` by Romain Michon, fused into one sample-and-hold).
//
//   Bit  : quantize amplitude to N bits     -> round(x * scale) / scale,
//                                              scale = 2^bits - 1
//   Freq : reduce the effective sample rate -> sample & hold every
//          decim = int(SR / freq) samples, holding the last quantized value
//
// At Bit = 16 and Freq >= host SR the effect is a true bypass (no coloration).
// Dependency-free and RT-safe: integer counter + a couple of std::round calls.

#include <algorithm>
#include <cmath>

namespace amyplug
{
class BitCrusher
{
public:
    BitCrusher() = default;

    void prepare(double sr) { sampleRate = sr > 1.0 ? sr : 48000.0; reset(); }

    void reset()
    {
        counter = 0;
        for (auto& c : chans) c = Channel {};
    }

    void setBits(float b)    { bits   = b; }     // 2..16  (16 = transparent)
    void setFreqHz(float hz) { freqHz = hz; }    // target sample rate (Hz)

    // Process up to two channels in place. Extra channels are left untouched.
    void process(float* const* channels, int numChannels, int numSamples)
    {
        // Faust: decim = int(1/rate) with rate = freq/SR, i.e. int(SR/freq).
        const int decim = std::max(1, (int) (sampleRate / std::max(1.0f, freqHz)));

        // True bypass when nothing would change the signal (default knob position).
        if (decim <= 1 && bits >= 16.0f) return;

        const double scale    = std::pow(2.0, (double) bits) - 1.0;
        const double invScale = scale > 0.0 ? 1.0 / scale : 1.0;
        const int    nch      = std::min(numChannels, 2);

        for (int i = 0; i < numSamples; ++i)
        {
            const bool sampleNow = (counter % (unsigned) decim) == 0;
            for (int ch = 0; ch < nch; ++ch)
                if (channels[ch] != nullptr)
                {
                    if (sampleNow)
                        chans[ch].held = std::round((double) channels[ch][i] * scale) * invScale;
                    channels[ch][i] = (float) chans[ch].held;
                }
            ++counter;
        }
    }

private:
    struct Channel { double held = 0.0; };

    double       sampleRate = 48000.0;
    float        bits = 16.0f, freqHz = 48000.0f;
    unsigned     counter = 0;          // shared sample index (S&H phase)
    Channel      chans[2];
};
} // namespace amyplug
