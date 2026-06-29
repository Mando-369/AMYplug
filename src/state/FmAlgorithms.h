// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// FmAlgorithms — the 32 DX7 operator topologies, mirrored from AMY's
// `algorithms[1..32]` (third_party/amy/src/algorithms.c). Used to label the FM
// algorithm menu with each algorithm's carrier operators (the ones routed to the
// output — the operators whose Level/Release you actually hear). Operator numbers
// here are DX7 1..6 and match the editor's OP 1..OP 6.

#include <juce_core/juce_core.h>
#include <cstdint>

namespace amyplug::fm
{
// Per-operator routing flags, ordered OP6..OP1 (as AMY stores them). kAlgoFlags[a-1]
// is DX7 algorithm a. A carrier outputs to the final mix, i.e. it writes to neither
// bus: (flags & (OUT_BUS_ONE|OUT_BUS_TWO)) == 0  ->  (flags & 0x03) == 0.
inline constexpr std::uint8_t kAlgoFlags[32][6] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, //  1
    { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, //  2
    { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 }, //  3
    { 0x41, 0x11, 0x94, 0x01, 0x11, 0x14 }, //  4
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, //  5
    { 0x41, 0x94, 0x01, 0x14, 0x01, 0x14 }, //  6
    { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, //  7
    { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }, //  8
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 }, //  9
    { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, // 10
    { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, // 11
    { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 }, // 12
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, // 13
    { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, // 14
    { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 }, // 15
    { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 }, // 16
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, // 17
    { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 }, // 18
    { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, // 19
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 }, // 20
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 }, // 21
    { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, // 22
    { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, // 23
    { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 }, // 24
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, // 25
    { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, // 26
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 }, // 27
    { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 }, // 28
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, // 29
    { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 }, // 30
    { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, // 31
    { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 }, // 32
};

// The full topology of an algorithm, reconstructed by simulating AMY's operator
// bus routing (render_algo). modulators[op] lists the operators that modulate `op`;
// carriers are the operators routed to the output; feedback[op] marks self-feedback.
struct AlgoTopology
{
    juce::Array<int> modulators[7];   // [op] = ops feeding op (op in 1..6)
    bool             feedback[7] {};  // [op] = op has self-feedback
    juce::Array<int> carriers;        // outputs (ascending)
};

inline AlgoTopology algorithmTopology(int a)
{
    AlgoTopology t;
    if (a < 1 || a > 32) return t;
    const std::uint8_t* fl = kAlgoFlags[a - 1];

    // Two modulation buses; each holds the operators currently written into it.
    juce::Array<int> bus1, bus2;
    for (int i = 0; i < 6; ++i)              // AMY processes ops in order OP6..OP1
    {
        const int op = 6 - i;
        const std::uint8_t f = fl[i];

        juce::Array<int>* in = (f & 0x10) ? &bus1 : (f & 0x20) ? &bus2 : nullptr;
        if (in != nullptr)
            for (int m : *in) t.modulators[op].add(m);   // bus writers modulate this op
        if (f & 0x40) t.feedback[op] = true;             // FB_IN -> self-feedback

        if (f & 0x01)        { if (! (f & 0x04)) bus1.clear(); bus1.add(op); }  // -> BUS_ONE
        else if (f & 0x02)   { if (! (f & 0x04)) bus2.clear(); bus2.add(op); }  // -> BUS_TWO
        else                 t.carriers.add(op);                                // -> output
    }
    t.carriers.sort();
    return t;
}

// Carrier operator numbers (DX7 1..6, ascending) for algorithm a (1..32).
inline juce::Array<int> algorithmCarriers(int a) { return algorithmTopology(a).carriers; }
} // namespace amyplug::fm
