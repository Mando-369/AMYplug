// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyEcho — AMY's bus echo (delay_line_in_out_fixed_delay in delay.c), transcribed
// to float as a standalone per-instance block. A fixed delay with feedback and a
// one-pole tone control applied in the feedback path: tone > 0 is a lowpass on the
// way into the delay, tone < 0 is a highpass FIR on the way out (AMY's exact three
// cases). Same behavior as the AMYplug bus echo.
//
// Derived from AMY (https://github.com/shorepine/amy), MIT. See NOTICES.md.

#include <vector>
#include <cmath>
#include <algorithm>

namespace amyplug
{
class AmyEcho
{
public:
    AmyEcho() = default;

    void prepare(double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        maxSamples = (int) std::ceil(sampleRate) + 2;   // up to 1 s of delay
        line.init(maxSamples);
        setParams(300.0f, 0.35f, 0.0f);
        reset();
    }

    void reset() { line.clear(); }

    void setParams(float delayMs, float feedback, float tone)
    {
        line.fixedDelay = clampi((int) std::lround(delayMs * sampleRate / 1000.0), 1, maxSamples - 1);
        fb = feedback;
        fc = tone;
    }

    // Mix the echo into the block in place at `level` (dry + level*wet). One instance
    // per channel (AMY runs the echo per channel with its own delay line).
    void process(float* block, int n, float level)
    {
        for (int i = 0; i < n; ++i)
        {
            const float dry      = block[i];
            const float delayOut = line.out(0);
            float mixOut = delayOut, writeVal;

            if (fc == 0.0f)
            {
                writeVal = dry + fb * delayOut;
            }
            else if (fc > 0.0f)
            {
                // Lowpass on the way in (pole on the +real axis).
                const float nextIn = dry + fb * delayOut;
                const float lastF  = line.lastWritten();
                writeVal = nextIn + fc * (lastF - nextIn);
            }
            else
            {
                // Highpass FIR on the way out (zero on the +real axis).
                const float output = delayOut + fc * line.out(1);
                writeVal = dry + fb * output;
                mixOut   = output;
            }

            line.in(writeVal);
            block[i] = dry + level * mixOut;
        }
    }

private:
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, nextIn = 0, fixedDelay = 1;
        void init(int minLen) { int len = 1; while (len < minLen) len <<= 1; buf.assign((size_t) len, 0.0f); mask = len - 1; nextIn = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); nextIn = 0; }
        inline float out(int extra) const { return buf[(size_t) ((nextIn - fixedDelay - extra) & mask)]; }
        inline float lastWritten() const  { return buf[(size_t) ((nextIn - 1) & mask)]; }
        inline void  in(float v) { buf[(size_t) nextIn] = v; nextIn = (nextIn + 1) & mask; }
    };

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    double sampleRate = 48000.0;
    int    maxSamples = 48002;
    DelayLine line;
    float  fb = 0.35f, fc = 0.0f;
};
} // namespace amyplug
