// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include "PresetRef.h"
#include "PatchLibrary.h"
#include <vector>

namespace amyplug
{
// Every preset the plugin can be "on", as ONE flat list: the factory banks in AMY's
// order, then the user banks with the top level first. The ‹ › arrows step through it
// and wrap; the browser field's menu and the Presets tab render the same list grouped.
// Built on demand from the library, never cached — a preset saved from a second
// instance, or dropped into the folder by hand, must show up without a reload.
struct PresetCatalog
{
    std::vector<PresetRef> items;

    static PresetCatalog build(const PatchLibrary& lib)
    {
        PresetCatalog c;
        c.items.reserve((size_t) kBuiltinPatchCount + lib.entries().size());
        for (int i = 0; i < kBuiltinPatchCount; ++i)
            c.items.push_back(PresetRef::factory(i));
        for (const auto& e : lib.entries())              // already sorted: root, then banks
            c.items.push_back(PresetRef::user(e.group, e.name));
        return c;
    }

    int indexOf(const PresetRef& ref) const
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i] == ref) return (int) i;
        return -1;
    }

    // The neighbour `delta` steps away, wrapping. A preset that is not in the list
    // (an orphan from another machine) steps from the top.
    PresetRef step(const PresetRef& from, int delta) const
    {
        if (items.empty()) return {};
        const int n = (int) items.size();
        int i = indexOf(from);
        i = (i < 0) ? (delta > 0 ? 0 : n - 1) : (((i + delta) % n) + n) % n;
        return items[(size_t) i];
    }

    // What a preset is, for the info panel: derived from the bank for factory patches
    // (the file is not opened), read from the file for user presets by the caller.
    static const char* factoryEngineName(int index) noexcept
    {
        if (index <= 127) return "Analog (Juno-6)";
        if (index <= 255) return "FM (DX7)";
        if (index == 256) return "Piano";
        return "AMYboard";
    }
};
} // namespace amyplug
