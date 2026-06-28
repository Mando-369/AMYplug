// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyWire.h"
#include "AmyConfig.h"
#include <cstdio>
#include <cstring>

namespace amyplug
{
void WireBuilder::append(const char* s)
{
    const int n = static_cast<int>(std::strlen(s));
    if (len + n >= static_cast<int>(buf.size())) return; // never overflow (drop instead)
    std::memcpy(buf.data() + len, s, static_cast<size_t>(n));
    len += n;
}

WireBuilder& WireBuilder::field(char code, int v)
{
    char tmp[24];
    std::snprintf(tmp, sizeof tmp, "%c%d", code, v);
    append(tmp);
    return *this;
}

WireBuilder& WireBuilder::field(char code, float v)
{
    // AMY accepts decimals; trim to a compact form. %g avoids trailing zeros.
    char tmp[32];
    std::snprintf(tmp, sizeof tmp, "%c%g", code, static_cast<double>(v));
    append(tmp);
    return *this;
}

WireBuilder& WireBuilder::field(const char* code, int v)
{
    char tmp[24];
    std::snprintf(tmp, sizeof tmp, "%s%d", code, v);
    append(tmp);
    return *this;
}

WireBuilder& WireBuilder::field(char code, const char* s)
{
    char tmp[2] = { code, 0 };
    append(tmp);
    append(s);
    return *this;
}

std::vector<std::uint8_t> WireBuilder::toSysex()
{
    return wrapSysex(str(), size());
}

std::vector<std::uint8_t> wrapSysex(const char* wire, int len)
{
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<size_t>(len) + 6);
    out.push_back(0xF0);
    out.push_back(kAmySysexId0);
    out.push_back(kAmySysexId1);
    out.push_back(kAmySysexId2);
    for (int i = 0; i < len; ++i)
        out.push_back(static_cast<std::uint8_t>(wire[i])); // wire is lower-ASCII, no encoding needed
    out.push_back(0xF7);
    return out;
}
} // namespace amyplug
