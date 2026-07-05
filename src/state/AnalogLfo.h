// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AnalogLfo — the Juno/analog LFO "mode" model. AMY's LFO is oscillator 1,
// replicated per voice (numVoices copies) and free-running; it has no native
// key-sync, global-sync, or tempo-sync. We synthesise the four classic modes on
// top of that primitive in the processor's streaming path (no rebuild):
//
//   * Poly — the raw AMY behaviour: each voice's LFO free-runs independently.
//   * Free — one global LFO: all voices phase-locked (a single `P0` aligns every
//            per-voice copy; identical freq off the same clock keeps them locked).
//   * Key  — the LFO restarts on every note-on (`P0` streamed per note-on). AMY's
//            `retrigger_mod_source` is only a stale header declaration — no C body
//            — so the phase-warp `P` message is the actual lever (amy.c warps the
//            live phase to the trigger value, not just the initial phase).
//   * Sync — Free's phase-lock, but the rate is locked to the host tempo:
//            freq = (bpm/60) / quartersPerCycle. The manual Freq knob is ignored.
//
// This header is the single source of truth for the mode/rate names and the sync
// math, so Parameters (choice names), the processor (streaming), and the tests all
// agree.

#include <juce_core/juce_core.h>

namespace amyplug::analoglfo
{
    // LFO mode. The index IS the stored/automation value — never reorder. Poly is
    // index 0 so a patch with no stored mode recalls the pre-M5 behaviour exactly.
    enum Mode { Poly = 0, Free = 1, Key = 2, Sync = 3 };

    inline juce::StringArray modeNames() { return { "Poly", "Free", "Key", "Sync" }; }

    // Tempo-sync note divisions. `quarters` = the length of one LFO cycle in quarter
    // notes, so a full cycle spans that many beats. Order is the stored index.
    struct Rate { const char* name; double quarters; };

    inline constexpr Rate rates[] = {
        { "4/1",   16.0        }, { "2/1",   8.0         }, { "1/1",   4.0        },
        { "1/2.",  3.0         }, { "1/2",   2.0         }, { "1/2T",  4.0 / 3.0  },
        { "1/4.",  1.5         }, { "1/4",   1.0         }, { "1/4T",  2.0 / 3.0  },
        { "1/8.",  0.75        }, { "1/8",   0.5         }, { "1/8T",  1.0 / 3.0  },
        { "1/16",  0.25        }, { "1/16T", 1.0 / 6.0   }, { "1/32",  0.125      },
    };
    inline constexpr int kNumRates = (int) (sizeof (rates) / sizeof (rates[0]));

    inline juce::StringArray rateNames()
    {
        juce::StringArray a;
        for (auto& r : rates) a.add (r.name);
        return a;
    }

    // Default rate = 1/4 (one cycle per beat), a musically neutral starting point.
    inline int defaultRateIndex()
    {
        for (int i = 0; i < kNumRates; ++i)
            if (juce::String (rates[i].name) == "1/4") return i;
        return 7;
    }

    // Convert a sync-rate index + host BPM to an LFO frequency in Hz, clamped to a
    // range AMY's oscillator tables render cleanly.
    inline double syncHz (int rateIndex, double bpm)
    {
        rateIndex = juce::jlimit (0, kNumRates - 1, rateIndex);
        const double q = rates[rateIndex].quarters;
        const double hz = (bpm / 60.0) / q;
        return juce::jlimit (0.01, 100.0, hz);
    }
}
