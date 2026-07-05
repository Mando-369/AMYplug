// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyFonts.h"
#include "BinaryData.h"
#include <juce_events/juce_events.h>   // DeletedAtShutdown + JUCE_DECLARE_SINGLETON
#include <map>

namespace amyplug::fonts
{
// The bundled typefaces are cached for the app's lifetime. They MUST be released
// while JUCE's font backend (CoreText on macOS) is still alive — a plain function-
// local static would destruct at __cxa_finalize, after JUCE has shut down, and
// crash. DeletedAtShutdown ties the cache to JUCE's GUI shutdown instead.
class FontLibrary final : public juce::DeletedAtShutdown
{
public:
    FontLibrary() = default;
    ~FontLibrary() override { clearSingletonInstance(); }

    juce::Font make(const char* file, float height)
    {
        auto& tf = cache[file];
        if (tf == nullptr) tf = load(file);
        if (tf == nullptr) return juce::Font(juce::FontOptions(height));   // fallback
        return juce::Font(juce::FontOptions(tf).withHeight(height));
    }

    JUCE_DECLARE_SINGLETON(FontLibrary, false)

private:
    // Find a bundled typeface by its original filename (robust against BinaryData's
    // symbol-name mangling).
    static juce::Typeface::Ptr load(const char* originalFilename)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const char* res = BinaryData::namedResourceList[i];
            if (juce::String(BinaryData::getNamedResourceOriginalFilename(res)) == originalFilename)
            {
                int size = 0;
                const char* data = BinaryData::getNamedResource(res, size);
                if (data != nullptr && size > 0)
                    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
            }
        }
        jassertfalse;   // font not bundled — check CMake juce_add_binary_data SOURCES
        return {};
    }

    std::map<juce::String, juce::Typeface::Ptr> cache;
};

JUCE_IMPLEMENT_SINGLETON(FontLibrary)

namespace
{
juce::Font make(const char* file, float height)
{
    if (auto* lib = FontLibrary::getInstance())
        return lib->make(file, height);
    return juce::Font(juce::FontOptions(height));   // after shutdown: harmless fallback
}
} // namespace

juce::Font logo(float h)   { return make("BarlowSemiCondensed-ExtraBold.ttf", h); }
juce::Font header(float h) { return make("BarlowSemiCondensed-Bold.ttf",      h); }
juce::Font label(float h)  { return make("BarlowCondensed-SemiBold.ttf",      h); }
juce::Font mono(float h)   { return make("IBMPlexMono-Regular.ttf",           h); }
juce::Font lcd(float h)    { return make("DSEG7Classic-Regular.ttf",          h); }
} // namespace amyplug::fonts
