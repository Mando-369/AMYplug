// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "Dx7Import.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>

namespace amyplug
{
namespace
{
// --- A DX7 operator's raw fields (already unpacked from either format) --------
struct RawOp
{
    int rates[4]  { 0, 0, 0, 0 };   // EG rates R1..R4 (0..99)
    int levels[4] { 0, 0, 0, 0 };   // EG levels L1..L4 (0..99)
    int outputLevel = 0;            // 0..99
    int mode    = 0;                // 0 = ratio, 1 = fixed
    int coarse  = 1;
    int fine    = 0;
    int detune  = 7;                // 0..14, centre 7
};
struct RawVoice
{
    RawOp ops[6];                   // stored DX7 order: ops[0] = OP6 ... ops[5] = OP1
    int   algorithm = 1;            // 1..32
    int   feedback  = 0;            // 0..7
    juce::String name;
};

// --- fm.py conversion math (ported) ------------------------------------------
// dx7level_to_linear: 0..99 -> linear amplitude, 99 -> 1.0.
double dx7LevelToLinear(int lvl) { return std::pow(2.0, (lvl - 99) / 8.0); }

// coarse_fine_ratio: harmonic ratio; coarse 0 is the 0.5 sub-octave special case.
double coarseFineRatio(int coarse, int fine, int detune)
{
    coarse &= 31;
    const double c = (coarse == 0) ? 0.5 : (double) coarse;
    return c * (1.0 + (fine + (detune - 7) / 8.0) / 100.0);
}
// coarse_fine_fixed_hz: fixed-frequency operators (10^(coarse + cents)).
double coarseFineFixedHz(int coarse, int fine, int detune)
{
    coarse &= 3;
    return std::pow(10.0, coarse + (fine + (detune - 7) / 8.0) / 100.0);
}
// feedback 0..7 -> AMY float.
double feedbackToFloat(int fb) { return 0.00125 * std::pow(2.0, (double) fb); }

// calc_loglin_eg_breakpoints segment timing (amp-EG defaults): exponential attack,
// linear decay. Returns seconds for one rate to move between two DX7 levels.
constexpr double kMinLevel = 34.0, kAttackRange = 75.0;
double attackTimeAtLevel(double level, double tConst)
{
    const double l = std::max(kMinLevel, level);
    return -tConst * std::log((kMinLevel + kAttackRange - l) / kAttackRange);
}
double egAttackSeconds(int rate, double fromLvl, double toLvl)
{
    const double tConst = 0.008 * std::pow(2.0, (65 - rate) / 6.0);
    return std::max(0.0, attackTimeAtLevel(toLvl, tConst) - attackTimeAtLevel(fromLvl, tConst));
}
double egDecaySeconds(int rate, double fromLvl, double toLvl)
{
    const double perSec = 0.5 + 0.5 * std::pow(2.0, rate / 6.0);   // rate_offset + rate_scale*2^(r/6)
    double diff = std::abs(toLvl - fromLvl);
    if (diff == 0.0) diff = 60.0;                                  // fm.py: release-from-rest fallback
    return diff / perSec;
}
double egSeconds(int rate, double fromLvl, double toLvl)
{
    return (toLvl > fromLvl) ? egAttackSeconds(rate, fromLvl, toLvl)
                             : egDecaySeconds(rate, fromLvl, toLvl);
}

PatchModel::FmOp convertOp(const RawOp& op)
{
    PatchModel::FmOp o;
    if (op.mode == 0)   // ratio mode
        o.ratio = juce::jlimit(0.1f, 16.0f, (float) coarseFineRatio(op.coarse, op.fine, op.detune));
    else                // fixed Hz — approximate as a note-relative ratio at A4 (won't key-track)
        o.ratio = juce::jlimit(0.1f, 16.0f, (float) (coarseFineFixedHz(op.coarse, op.fine, op.detune) / 440.0));

    o.level = juce::jlimit(0.0f, 4.0f, (float) (2.0 * dx7LevelToLinear(op.outputLevel)));

    // DX7 EG (R1..R4 / L1..L4) -> A/D/S/R. L4 is the start/release floor (usually 0).
    const double L1 = op.levels[0], L2 = op.levels[1], L3 = op.levels[2], L4 = op.levels[3];
    o.a = juce::jlimit(0.0f, 10.0f, (float) egSeconds(op.rates[0], L4, L1));
    o.d = juce::jlimit(0.0f, 10.0f, (float) (egSeconds(op.rates[1], L1, L2)
                                           + egSeconds(op.rates[2], L2, L3)));
    o.s = juce::jlimit(0.0f, 1.0f, (float) dx7LevelToLinear(op.levels[2]));
    o.r = juce::jlimit(0.0f, 10.0f, (float) egSeconds(op.rates[3], L3, L4));
    return o;
}

Dx7Voice convertVoice(const RawVoice& raw)
{
    Dx7Voice v;
    v.name = raw.name.trimEnd();
    v.fm.algorithm = juce::jlimit(1, 32, raw.algorithm);
    v.fm.feedback  = juce::jlimit(0.0f, 1.0f, (float) feedbackToFloat(raw.feedback));
    // raw.ops is stored OP6..OP1; our FmParams.ops[i] is DX7 operator (i+1), so
    // ops[i] = raw.ops[5 - i] (matches emitFm's O6,5,4,3,2,1 routing).
    for (int i = 0; i < 6; ++i)
        v.fm.ops[i] = convertOp(raw.ops[5 - i]);
    return v;
}

juce::String readName(const std::uint8_t* p, int n)
{
    juce::String s;
    for (int i = 0; i < n; ++i)
    {
        const auto c = p[i];
        s += (c >= 32 && c < 127) ? (juce::juce_wchar) c : (juce::juce_wchar) ' ';
    }
    return s;
}

// --- Unpack one packed bulk voice (128 bytes) --------------------------------
RawVoice unpackPacked(const std::uint8_t* d)
{
    RawVoice v;
    for (int k = 0; k < 6; ++k)                  // ops stored OP6..OP1, 17 bytes each
    {
        const std::uint8_t* o = d + k * 17;
        RawOp& op = v.ops[k];
        for (int i = 0; i < 4; ++i) { op.rates[i] = o[i]; op.levels[i] = o[4 + i]; }
        op.detune      = (o[12] >> 3) & 0x0F;    // byte12: bits0-2 rate-scale, bits3-6 detune
        op.outputLevel = o[14];
        op.mode        =  o[15] & 0x01;          // byte15: bit0 mode, bits1-5 coarse
        op.coarse      = (o[15] >> 1) & 0x1F;
        op.fine        =  o[16];
    }
    v.algorithm = (d[110] & 0x1F) + 1;           // stored 0..31
    v.feedback  =  d[111] & 0x07;                // byte111: bits0-2 feedback
    v.name      = readName(d + 118, 10);
    return v;
}

// --- Read one unpacked VCED voice (155 bytes) --------------------------------
RawVoice readVced(const std::uint8_t* d)
{
    RawVoice v;
    for (int k = 0; k < 6; ++k)                  // ops stored OP6..OP1, 21 bytes each
    {
        const std::uint8_t* o = d + k * 21;
        RawOp& op = v.ops[k];
        for (int i = 0; i < 4; ++i) { op.rates[i] = o[i]; op.levels[i] = o[4 + i]; }
        op.outputLevel = o[16];
        op.mode        = o[17];                  // 0 = ratio, 1 = fixed
        op.coarse      = o[18];
        op.fine        = o[19];
        op.detune      = o[20];
    }
    v.algorithm = d[134] + 1;                    // stored 0..31
    v.feedback  = d[135];
    v.name      = readName(d + 145, 10);
    return v;
}
} // namespace

std::vector<Dx7Voice> Dx7Import::parse(const void* data, size_t size)
{
    std::vector<Dx7Voice> out;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (bytes == nullptr || size == 0) return out;

    // Find the payload + format. Standard wrappers:
    //   bulk 32-voice : F0 43 0n 09 20 00 <4096> cksum F7   (format byte = 0x09)
    //   single VCED   : F0 43 0n 00 01 1B <155>  cksum F7   (format byte = 0x00)
    // Also accept the raw payload with no F0/F7 wrapper.
    const std::uint8_t* payload = bytes;
    size_t payloadSize = size;
    bool   isBulk = false, known = false;

    if (size >= 6 && bytes[0] == 0xF0 && bytes[1] == 0x43)
    {
        const std::uint8_t fmt = bytes[3];
        if (fmt == 0x09 && size >= 6 + 4096) { payload = bytes + 6; payloadSize = 4096; isBulk = true;  known = true; }
        else if (fmt == 0x00 && size >= 6 + 155) { payload = bytes + 6; payloadSize = 155; isBulk = false; known = true; }
    }
    if (! known)   // unwrapped payload — guess by size
    {
        if (size >= 4096)      { payload = bytes; payloadSize = 4096; isBulk = true;  known = true; }
        else if (size >= 155)  { payload = bytes; payloadSize = 155;  isBulk = false; known = true; }
    }
    if (! known) return out;

    if (isBulk)
    {
        const int voices = (int) (payloadSize / 128);
        for (int i = 0; i < voices && i < 32; ++i)
            out.push_back(convertVoice(unpackPacked(payload + i * 128)));
    }
    else
    {
        out.push_back(convertVoice(readVced(payload)));
    }
    return out;
}

std::vector<Dx7Voice> Dx7Import::parseFile(const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData(mb)) return {};
    return parse(mb.getData(), mb.getSize());
}

PatchModel Dx7Import::toPatchModel(const Dx7Voice& voice)
{
    PatchModel m;
    auto& s = m.synths[0];
    s.engine = PatchModel::Engine::FM;
    s.fm     = voice.fm;
    // Leave the master amp envelope (on the ALGO osc) wide open so the operator
    // envelopes do the shaping; a short release still guarantees a clean note-off.
    s.ampAttack = 0.001f; s.ampDecay = 0.001f; s.ampSustain = 1.0f; s.ampRelease = 0.05f;
    return m;
}

// ---------------------------------------------------------------------------
// Factory-patch wire decode: AMY's patch_commands string -> our FmParams.
// ---------------------------------------------------------------------------
namespace
{
// One osc's fields, parsed from a "v<n>...Z" token. A wire field is a letter
// followed by its value chars ([0-9 . , + -]); a new letter starts the next field.
struct OscFields
{
    int osc = -1;
    std::map<char, juce::String> f;   // field letter -> value string
};

OscFields scanOsc(const juce::String& tok)
{
    OscFields o;
    const int n = tok.length();
    int i = 0;
    while (i < n)
    {
        if (juce::CharacterFunctions::isLetter(tok[i]))
        {
            const char letter = (char) tok[i];
            const int start = ++i;
            while (i < n && ! juce::CharacterFunctions::isLetter(tok[i])) ++i;
            o.f[letter] = tok.substring(start, i);
        }
        else ++i;
    }
    if (o.f.count('v')) o.osc = o.f['v'].getIntValue();
    return o;
}

// First comma-separated value of a coef list ("0.458,0,0,1" -> 0.458).
float firstVal(const juce::String& list)
{
    return list.upToFirstOccurrenceOf(",", false, false).getFloatValue();
}

// Reduce an AMY breakpoint list "t0,l0,t1,l1,..." (ms,level pairs; the LAST pair is
// the release, fired on note-off) to A/D/S/R in seconds. Heuristic but stable for the
// factory DX7 bank: attack = time to the peak level, sustain = level held before the
// final segment, decay = time from peak to that level, release = the final segment.
void bpToAdsr(const juce::String& bp, float& a, float& d, float& s, float& r)
{
    juce::StringArray parts; parts.addTokens(bp, ",", "");
    std::vector<float> t, l;
    for (int i = 0; i + 1 < parts.size(); i += 2)
    {
        t.push_back(juce::jmax(0.0f, parts[i].getFloatValue()));
        l.push_back(juce::jlimit(0.0f, 1.0f, parts[i + 1].getFloatValue()));
    }
    if (l.empty()) return;                                   // keep defaults
    const int last = (int) l.size() - 1;
    int peak = 0;
    for (int i = 1; i <= last; ++i) if (l[i] > l[peak]) peak = i;
    float atk = 0.0f; for (int i = 0; i <= peak; ++i) atk += t[i];
    float dec = 0.0f; for (int i = peak + 1; i <= last - 1; ++i) dec += t[i];
    const float rel = (last >= 1) ? t[last]     : 50.0f;
    const float sus = (last >= 1) ? l[last - 1] : l[last];
    a = juce::jmax(0.0f, atk) * 0.001f;
    d = juce::jmax(0.0f, dec) * 0.001f;
    s = juce::jlimit(0.0f, 1.0f, sus);
    r = juce::jmax(1.0f, rel) * 0.001f;
}
} // namespace

bool factoryFmWireToParams(const juce::String& wire, PatchModel::FmParams& out)
{
    juce::StringArray toks; toks.addTokens(wire, "Z", "");
    std::map<int, OscFields> oscs;
    int algoOsc = -1;
    for (auto& tk : toks)
    {
        if (tk.isEmpty()) continue;
        OscFields o = scanOsc(tk);
        if (o.osc < 0) continue;
        oscs[o.osc] = o;
        if (o.f.count('o') && o.f.count('O')) algoOsc = o.osc;   // the ALGO controller
    }
    if (algoOsc < 0) return false;                            // no algorithm -> not FM (e.g. Juno)
    const auto& algo = oscs[algoOsc].f;

    PatchModel::FmParams fm;
    fm.algorithm = juce::jlimit(1, 32, algo.at('o').getIntValue());
    if (algo.count('b')) fm.feedback = juce::jlimit(0.0f, 1.0f, algo.at('b').getFloatValue());

    // Map oscillators to OP1..6 via the patch's O source list. AMY lists algo_source
    // 6->1 (source k is DX7 operator 6-k); our ops[i] is DX7 operator i+1, so ops[i]
    // takes the osc named at O position (kFmOps-1-i).
    juce::StringArray src; src.addTokens(algo.at('O'), ",", "");
    if (src.size() != PatchModel::kFmOps) return false;

    int decoded = 0;
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int oscIdx = src[PatchModel::kFmOps - 1 - i].getIntValue();
        auto it = oscs.find(oscIdx);
        if (it == oscs.end()) continue;
        const auto& f = it->second.f;
        auto& op = fm.ops[i];
        if (f.count('I')) op.ratio = juce::jmax(0.0f, f.at('I').getFloatValue());
        if (f.count('a')) op.level = juce::jlimit(0.0f, 4.0f, firstVal(f.at('a')));
        if (f.count('A')) bpToAdsr(f.at('A'), op.a, op.d, op.s, op.r);
        ++decoded;
    }
    if (decoded == 0) return false;

    out = fm;
    return true;
}
} // namespace amyplug
