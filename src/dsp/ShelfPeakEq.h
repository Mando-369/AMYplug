// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// ShelfPeakEq — the plugin's 3-band master EQ: low shelf -> peaking bell -> high shelf,
// three state-variable filters IN SERIES.
//
// ⚠️ Why this exists rather than AMY's `x<low,mid,high>`.
//
// AMY runs its three EQ bands in PARALLEL and sums them, `LPF - BPF + HPF`
// (third_party/amy/src/filters.c:575). That is a complementary decomposition: the three
// paths reconstruct the input only while their gains are EQUAL. Scale one band on its own
// and the cancellation that made it work becomes a notch. Measured on AMY 1.2.16, relative
// to a flat reference:
//
//     low +1 dB  ->  -5.9 dB @ 800 Hz,  -7.7 dB @ 1.5 kHz
//     low +6 dB  ->             +0.9,  -15.1 dB @ 1.5 kHz
//     all +6 dB  ->  +6 dB, essentially flat   (equal gains, so it reconstructs)
//
// A 1 dB boost digging an 8 dB hole an octave and a half up is not an EQ. Full write-up and
// a proposed upstream fix: docs/upstream/AMY-eq-issue.md.
//
// In SERIES the magnitudes multiply, so each control moves only its own region, the
// response is monotonic in the control, and 0/0/0 is exactly unity — no cancellation is
// possible because there is only one path.
//
// Topology: Andrew Simper's trapezoidal-integration SVF (Cytomic). One structure yields
// shelf and bell by mixing its own states, it is unconditionally stable, and it stays
// well-behaved at high gain and low frequency where a Direct-Form biquad does not.
// RT-safe: no allocation, no branching per sample, coefficients recomputed only on change.

#include <cmath>
#include <algorithm>

namespace amyplug
{
class SvfBand
{
public:
    enum class Mode { LowShelf, Bell, HighShelf };

    void prepare(double sr) { sampleRate = sr > 1.0 ? sr : 48000.0; dirty = true; reset(); }
    void reset() { for (auto& s : st) s = State {}; }

    void setMode(Mode m)       { if (m != mode) { mode = m; dirty = true; } }
    void setFreq(float hz)     { if (hz != freq) { freq = hz; dirty = true; } }
    void setQ(float q)         { if (q != Q)     { Q = q;     dirty = true; } }
    void setGainDb(float db)   { if (db != gainDb) { gainDb = db; dirty = true; } }

    // True when this band is doing nothing, so the caller can skip it entirely.
    bool isUnity() const { return std::abs(gainDb) < 1.0e-4f; }

    void process(float* const* channels, int numChannels, int numSamples)
    {
        if (dirty) updateCoefficients();
        const int n = std::min(numChannels, 2);
        for (int c = 0; c < n; ++c)
        {
            float* x = channels[c];
            if (x == nullptr) continue;
            auto& s = st[(size_t) c];
            for (int i = 0; i < numSamples; ++i)
            {
                const float v0 = x[i];
                const float v3 = v0 - s.ic2;
                const float v1 = a1 * s.ic1 + a2 * v3;
                const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;
                x[i] = m0 * v0 + m1 * v1 + m2 * v2;
            }
            // The integrator states are the only thing that can go non-finite (a nonsense
            // sample rate, a denormal storm); clear rather than let it propagate to the DAC.
            if (! std::isfinite(s.ic1) || ! std::isfinite(s.ic2)) s = State {};
        }
    }

private:
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };

    void updateCoefficients()
    {
        dirty = false;
        const float nyquist = (float) sampleRate * 0.5f;
        const float fc = std::clamp(freq, 20.0f, nyquist * 0.95f);
        const float A  = std::pow(10.0f, gainDb / 40.0f);       // amplitude, not power
        const float q  = std::max(0.05f, Q);

        // Cytomic SVF: g pre-warps the cutoff, k is the damping, and the m-coefficients
        // mix v0/v1/v2 into the response. The shelves shift g by sqrt(A) so the shelf
        // MIDPOINT lands on fc rather than the corner drifting with gain.
        float g = std::tan(3.14159265358979f * fc / (float) sampleRate);
        float k = 1.0f / q;
        switch (mode)
        {
            case Mode::LowShelf:
                g /= std::sqrt(A);
                m0 = 1.0f;      m1 = k * (A - 1.0f);       m2 = A * A - 1.0f;
                break;
            case Mode::HighShelf:
                g *= std::sqrt(A);
                m0 = A * A;     m1 = k * (1.0f - A) * A;   m2 = 1.0f - A * A;
                break;
            case Mode::Bell:
            default:
                k  = 1.0f / (q * A);                       // Q is constant-Q against gain
                m0 = 1.0f;      m1 = k * (A * A - 1.0f);   m2 = 0.0f;
                break;
        }
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 48000.0;
    Mode  mode   = Mode::Bell;
    float freq   = 1000.0f, Q = 0.707f, gainDb = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    State st[2] {};
    bool  dirty = true;
};

// The master EQ. Centres match AMY's (800 / 2500 / 7000 Hz) so a patch that carries an
// `x` tilt in its own wire lands on the bands it was voiced for.
class ShelfPeakEq
{
public:
    static constexpr float kLowHz = 800.0f, kMidHz = 2500.0f, kHighHz = 7000.0f;

    void prepare(double sr)
    {
        low.setMode(SvfBand::Mode::LowShelf);   low.setFreq(kLowHz);   low.setQ(0.707f);
        mid.setMode(SvfBand::Mode::Bell);       mid.setFreq(kMidHz);   mid.setQ(0.9f);
        high.setMode(SvfBand::Mode::HighShelf); high.setFreq(kHighHz); high.setQ(0.707f);
        low.prepare(sr); mid.prepare(sr); high.prepare(sr);
    }

    void reset() { low.reset(); mid.reset(); high.reset(); }

    void setGainsDb(float lowDb, float midDb, float highDb)
    { low.setGainDb(lowDb); mid.setGainDb(midDb); high.setGainDb(highDb); }

    // Series, in that order. A band at exactly 0 dB is skipped — it is a true bypass, not
    // an approximation, which is what makes "all three flat" bit-identical to no EQ at all.
    void process(float* const* channels, int numChannels, int numSamples)
    {
        if (! low.isUnity())  low.process(channels, numChannels, numSamples);
        if (! mid.isUnity())  mid.process(channels, numChannels, numSamples);
        if (! high.isUnity()) high.process(channels, numChannels, numSamples);
    }

private:
    SvfBand low, mid, high;
};
} // namespace amyplug
