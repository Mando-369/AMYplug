// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_core/juce_core.h>
#include "BuiltinPatchNames.h"

namespace amyplug
{
// Which preset the plugin is "on" — the thing the browser field names.
//
// Deliberately NOT derived from the parameters. Two presets can hold identical values,
// and after a session restore the parameters are what the user last heard while the name
// is a label for where they came from. It is persisted beside the parameters and
// re-resolved by name on restore; the preset is never re-loaded from disk then, because a
// file of the same name on this machine may not be the same file
// (Code Repo/JUCE-UI-LnF__14 §8).
struct PresetRef
{
    enum class Source { None, Factory, User };

    Source       source       = Source::None;
    juce::String bank;               // factory bank, or the user bank ("" = top level)
    juce::String name;               // display name
    int          factoryIndex = -1;  // 0..kBuiltinPatchCount-1 when source == Factory

    static PresetRef factory(int index);
    static PresetRef user(juce::String bank, juce::String name);

    bool isNone()    const noexcept { return source == Source::None; }
    bool isFactory() const noexcept { return source == Source::Factory; }
    bool isUser()    const noexcept { return source == Source::User; }

    bool operator==(const PresetRef& o) const noexcept
    { return source == o.source && factoryIndex == o.factoryIndex && bank == o.bank && name == o.name; }
    bool operator!=(const PresetRef& o) const noexcept { return ! (*this == o); }
};

// AMY's built-in patches are one flat list; the four banks are fixed ranges of it.
inline const char* factoryBankOf(int patchNumber) noexcept
{
    if (patchNumber <= 127) return "Juno";
    if (patchNumber <= 255) return "DX7";
    if (patchNumber == 256) return "Piano";
    return "AMYboard";
}

inline PresetRef PresetRef::factory(int index)
{
    PresetRef r;
    if (index < 0 || index >= kBuiltinPatchCount) return r;
    r.source       = Source::Factory;
    r.factoryIndex = index;
    r.bank         = factoryBankOf(index);
    r.name         = kBuiltinPatchNames[index];
    return r;
}

inline PresetRef PresetRef::user(juce::String bank, juce::String name)
{
    PresetRef r;
    if (name.isEmpty()) return r;
    r.source = Source::User;
    r.bank   = std::move(bank);
    r.name   = std::move(name);
    return r;
}
} // namespace amyplug
