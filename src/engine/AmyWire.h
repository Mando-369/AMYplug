// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyWire — the ONE place that knows AMY's wire letter-codes. Builds compact
// ASCII wire messages (e.g. "v0w0f440l1Z") from typed parameters, and wraps them
// as MIDI SysEx for Hardware mode. Keep all AMY-letter knowledge here.
//
// Reference: docs/AMY_WIRE_PROTOCOL.md (full table).

#include "AmyConstants.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace amyplug
{
// A small fixed-capacity ASCII builder so it can be used without allocation on
// the RT thread (use a stack instance). Capacity matches AMY's 255-byte message
// limit.
class WireBuilder
{
public:
    WireBuilder& osc(int v)            { return field('v', v); }
    WireBuilder& synth(int v)          { return field('i', v); }
    WireBuilder& numVoices(int v)      { return field("iv", v); }
    WireBuilder& wave(amy::Wave w)     { return field('w', static_cast<int>(w)); }
    WireBuilder& note(float midi)      { return field('n', midi); }
    WireBuilder& velocity(float v)     { return field('l', v); }
    WireBuilder& patch(int p)          { return field('K', p); }
    WireBuilder& freq(float hz)        { return field('f', hz); }
    WireBuilder& filterFreq(float hz)  { return field('F', hz); }
    WireBuilder& filterType(amy::Filter f) { return field('G', static_cast<int>(f)); }
    WireBuilder& resonance(float q)    { return field('R', q); }
    WireBuilder& duty(float d)         { return field('d', d); }
    WireBuilder& pan(float p)          { return field('Q', p); }
    WireBuilder& volume(float v)       { return field('V', v); }   // global
    WireBuilder& reverb(float v)       { return field('h', v); }   // per-bus level
    WireBuilder& chorus(float v)       { return field('k', v); }
    WireBuilder& echo(float v)         { return field('M', v); }
    WireBuilder& bp0(const char* s)    { return field('A', s); }
    WireBuilder& bp1(const char* s)    { return field('B', s); }
    WireBuilder& pitchBend(float v)    { return field('s', v); }
    WireBuilder& reset(amy::Reset r)   { return field('S', static_cast<int>(r)); }
    WireBuilder& raw(const char* s)    { append(s); return *this; }

    // Terminate (AMY messages end with 'Z') and return the C string.
    const char* str()  { if (! terminated) { append("Z"); terminated = true; } buf[len] = 0; return buf.data(); }
    int         size() const { return len; }
    void        clear() { len = 0; terminated = false; }

    // Build the SysEx frame (F0 00 03 45 <wire> F7) for Hardware mode.
    std::vector<std::uint8_t> toSysex();

private:
    WireBuilder& field(char code, int v);
    WireBuilder& field(char code, float v);
    WireBuilder& field(const char* code, int v);  // two-letter codes like "iv"
    WireBuilder& field(char code, const char* s);
    void append(const char* s);

    std::array<char, 256> buf {};
    int  len = 0;
    bool terminated = false;
};

// Helpers that wrap an existing ASCII wire string for SysEx transport.
std::vector<std::uint8_t> wrapSysex(const char* wire, int len);
} // namespace amyplug
