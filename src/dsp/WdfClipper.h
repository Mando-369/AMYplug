// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// WdfClipper — a Wave Digital Filter antiparallel-diode (LED) clipper, ported
// from the Kalos mastering plugin's Faust source (KalosSoftClipper.dsp, author
// Thomas Mandolini; diode WDF model by Dirk Roosenburg, MIT-style STK-4.3).
//
// Circuit:  Vin --[R=7.5k]--+-- Vout
//                            |
//                          [D1//D2]  (antiparallel LEDs)
//                            |
//                           GND
//
// Signal chain per channel (matches the Faust graph exactly):
//
//   x -> *(drive) -> WDF diode clip -> 5 Hz DC-block (SVF TPT HP2) -> /(drive)
//        -> hard clip at the output ceiling
//
// "drive" is the THD control (dB -> linear, one-pole smoothed). Multiplying into
// the diodes and dividing back out afterwards is the GAIN COMPENSATION: more
// drive = more saturation harmonics at a steady output level. The diode solve is
// Faust's generated Lambert-W / Newton iteration; the magic constants below are
// transcribed verbatim from the generated KalosSoftClipper.cpp (Faust 2.85.5),
// so this is bit-faithful to Kalos.
//
// Dependency-free (only <cmath>/<algorithm>) and RT-safe: no allocation, no
// locks — just transcendental math, the same class AMY uses internally.

#include <algorithm>
#include <cmath>

namespace amyplug
{
class WdfClipper
{
public:
    WdfClipper() = default;

    // Compute the sample-rate-dependent constants and snap the smoothers to the
    // current target gains (no startup fade). Call from prepareToPlay.
    void prepare(double sampleRate)
    {
        const double sr = std::min(1.92e+05, std::max(1.0, sampleRate));
        fConst1 = 44.1 / sr;
        fConst2 = 1.0 - fConst1;
        const double t  = std::tan(15.707963267948966 / sr);   // 5 Hz TPT HP
        fConst4 = t + 1.3513513513513513;
        const double c5 = t * fConst4 + 1.0;
        fConst6 = t / c5;
        fConst7 = 2.0 * fConst6;
        fConst8 = 2.0 * t;
        fConst9 = 1.0 / c5;
        reset();
    }

    void reset()
    {
        smCeil = db2lin(outDb);
        smDrive = db2lin(driveDb);
        for (auto& c : chans) c = Channel {};
    }

    // THD / Drive in dB. Negative = cleaner (signal pulled below the diode knee),
    // positive = more saturation. With gain compensation ON (default) the post-divide
    // keeps the level steady (Kalos behavior); OFF, drive pushes level straight into
    // the diodes and out — clipping hotter and louder, like running a synth's output
    // volume into a clipper. The AMYplug instrument leaves it ON; AMYplugFX turns it OFF.
    void setDriveDb(float dB)   { driveDb = dB; }
    // Output ceiling in dB (<= 0). The diode clip never exceeds it.
    void setOutputDb(float dB)  { outDb = dB; }
    // Gain compensation: divide the drive back out after the diode (steady level) or not.
    void setGainCompensation(bool on) { compensate = on; }

    // Process up to two channels in place. Extra channels are left untouched.
    void process(float* const* channels, int numChannels, int numSamples)
    {
        const double targCeil  = fConst1 * db2lin(outDb);
        const double targDrive = fConst1 * db2lin(driveDb);
        const int    nch       = std::min(numChannels, 2);

        for (int i = 0; i < numSamples; ++i)
        {
            // One-pole smooth the ceiling + drive once per frame (shared across
            // channels, exactly as the Faust graph does).
            smCeil  = targCeil  + fConst2 * smCeil;
            smDrive = targDrive + fConst2 * smDrive;

            for (int ch = 0; ch < nch; ++ch)
                if (channels[ch] != nullptr)
                    channels[ch][i] = (float) processSample((double) channels[ch][i],
                                                            smDrive, smCeil, chans[ch]);
        }
    }

private:
    struct Channel
    {
        double in1 = 0.0;          // driven input, one sample of memory (fRec4)
        double hp1 = 0.0, hp2 = 0.0; // SVF TPT highpass state (fRec1, fRec2)
    };

    static double db2lin(double dB) { return std::pow(10.0, 0.05 * dB); }

    // The WDF antiparallel-diode reflection term, evaluated on the previous driven
    // sample. Transcribed verbatim from the Faust-generated compute() (constants
    // included), so the saturation curve matches Kalos exactly.
    static double diodeReflect(double prev)
    {
        const int s = (prev > 0.0) - (prev < 0.0);

        // Forward-conducting branch.
        const double e1 = std::exp(21.49151085321298 * prev * (double) s);
        const double f3 = 4.771115409413281e-07 * e1;
        const double f4 = std::sqrt(std::max(0.0, 2.0 * (1.296923631888906e-06 * e1 + 1.0)));
        const double f5 = (f3 < 1.0) ? f4 * (f4 * (0.1527777777777778 * f4 - 0.3333333333333333) + 1.0) - 1.0
                                     : std::log(std::fabs(f3));
        const double f6 = std::exp(f5);
        const double f7 = f5 * f6 - f3;
        const double f8 = f5 + 1.0;
        const double f9 = f7 / (f6 * f8 - 0.5 * (f7 * (f5 + 2.0) / f8));
        const double f10 = f5 - f9;
        const double f11 = std::exp(f10);
        const double f12 = f10 * f11 - f3;
        const double f13 = f5 + (1.0 - f9);
        const double f14 = f9 + f12 / (f11 * f13 - 0.5 * (f12 * (f5 + (2.0 - f9)) / f13));
        const double f15 = f5 - f14;
        const double f16 = std::exp(f15);
        const double f17 = f15 * f16 - f3;
        const double f18 = f5 + (1.0 - f14);
        const double f19 = f3 + 0.36787944117144233;
        const double f20 = std::sqrt(std::max(0.0, f19));

        // Reverse-conducting branch.
        const double e21 = std::exp(21.49151085321298 * prev * (double) (-s));
        const double f22 = 4.771115409413281e-07 * e21;
        const double f23 = std::sqrt(std::max(0.0, 2.0 * (1.0 - 1.296923631888906e-06 * e21)));
        const double f24 = (-f22 < 1.0) ? f23 * (f23 * (0.1527777777777778 * f23 - 0.3333333333333333) + 1.0) - 1.0
                                        : std::log(std::fabs(-f22));
        const double f25 = std::exp(f24);
        const double f26 = f22 + f24 * f25;
        const double f27 = f24 + 1.0;
        const double f28 = f26 / (f25 * f27 - 0.5 * (f26 * (f24 + 2.0) / f27));
        const double f29 = f24 - f28;
        const double f30 = std::exp(f29);
        const double f31 = f22 + f29 * f30;
        const double f32 = f24 + (1.0 - f28);
        const double f33 = f28 + f31 / (f30 * f32 - 0.5 * (f31 * (f24 + (2.0 - f28)) / f32));
        const double f34 = f24 - f33;
        const double f35 = std::exp(f34);
        const double f36 = f22 + f34 * f35;
        const double f37 = f24 + (1.0 - f33);
        const double f38 = 0.36787944117144233 - f22;
        const double f39 = std::sqrt(std::max(0.0, f38));

        const double pos = (f3 < -0.36777944117144235)
            ? 2.331643981597124 * f20 + f19 * (1.9366311144923598 * f20 - 1.8121878856393634 - 2.3535512018816145 * f19) - 1.0
            : f5 - (f14 + f17 / (f16 * f18 - 0.5 * (f17 * (f5 + (2.0 - f14)) / f18)));
        const double neg = (-f22 < -0.36777944117144235)
            ? 2.331643981597124 * f39 + f38 * (1.9366311144923598 * f39 - 1.8121878856393634 - 2.3535512018816145 * f38) - 1.0
            : f24 - (f33 + f36 / (f35 * f37 - 0.5 * (f36 * (f24 + (2.0 - f33)) / f37)));
        return pos + neg;
    }

    double processSample(double x, double drive, double ceil, Channel& c)
    {
        const double driven = x * drive;
        const int    s      = (c.in1 > 0.0) - (c.in1 < 0.0);
        const double resolved = 0.5 * (driven + (c.in1 - 0.04653 * (double) (2 * s) * diodeReflect(c.in1)));
        c.in1 = driven;

        // 5 Hz SVF TPT highpass (DC blocker). The bandpass tap uses the OLD hp1,
        // so read it before updating the integrators.
        const double v    = resolved - (fConst4 * c.hp1 + c.hp2);
        const double band = c.hp1 + fConst6 * v;
        c.hp1 += fConst7 * v;
        c.hp2 += fConst8 * band;
        const double hp   = fConst9 * v;

        // Gain compensation (divide back out) keeps level steady; without it, drive
        // pushes level into the diode + output ceiling — clips hotter and louder.
        const double y = compensate ? (hp / drive) : hp;
        return std::max(-ceil, std::min(ceil, y));
    }

    // Smoothed gains (shared) and per-channel diode/HP state.
    double fConst1 = 0.0, fConst2 = 1.0;
    double fConst4 = 0.0, fConst6 = 0.0, fConst7 = 0.0, fConst8 = 0.0, fConst9 = 0.0;
    double smCeil = 1.0, smDrive = 1.0;
    float  outDb = 0.0f, driveDb = 0.0f;
    bool   compensate = true;   // instrument: true (Kalos); AMYplugFX: false
    Channel chans[2];
};
} // namespace amyplug
