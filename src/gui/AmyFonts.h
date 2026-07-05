// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_graphics/juce_graphics.h>

// Bundled typefaces (BinaryData, wired via juce_add_binary_data in CMakeLists).
// The .ttf files live in assets/fonts and are OFL-licensed (see assets/fonts/OFL-*.txt
// and DSEG-LICENSE.txt). Loaded once and cached.
namespace amyplug::fonts
{
// Barlow Semi Condensed 800 — logo.
juce::Font logo(float height);
// Barlow Semi Condensed 700 — section header bars (UPPERCASE, tracked).
juce::Font header(float height);
// Barlow Condensed 600 — control labels (UPPERCASE, tracked).
juce::Font label(float height);
// IBM Plex Mono 400 — mono captions / fallback numerics.
juce::Font mono(float height);
// DSEG7 Classic — segmented LCD numeric readouts.
juce::Font lcd(float height);
} // namespace amyplug::fonts
