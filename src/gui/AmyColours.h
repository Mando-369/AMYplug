// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_graphics/juce_graphics.h>

// Design tokens from the AMYplug visual handoff (visual/design_handoff_amyplug).
// All colours are the final, high-fidelity values from README.md § Design tokens.
namespace amyplug::colours
{
using C = juce::Colour;

// --- shell / chrome (deliberately neutral, no engine colour) ---------------
inline const C shellTop      { 0xff16191e };   // window body top of gradient
inline const C shellBottom   { 0xff101317 };   // window body bottom of gradient
inline const C panel         { 0xff0f1318 };   // panel / card fill
inline const C panelRaised   { 0xff1a1f25 };   // raised buttons
inline const C groove        { 0xff080b0e };   // LCD / dropdown wells (inset)
inline const C hairline      { 0xff232c35 };   // hairline border

// --- text ------------------------------------------------------------------
inline const C textPrimary   { 0xffe7ecf2 };
inline const C textDim        { 0xff8a95a2 };  // labels
inline const C textFaint      { 0xff5a636e };  // carets, captions

// --- functional / section accents ------------------------------------------
inline const C engineCyan    { 0xff37c2d4 };   // DX7 identity
inline const C junoRed        { 0xffd23b34 };  // oscillators
inline const C junoBlue       { 0xff4f74e0 };  // envelopes (AMP ENV)
inline const C filterViolet   { 0xff8a5cd6 };  // VCF / VCF ENV
inline const C lfoGreen       { 0xff83cc9c };  // LFO
inline const C amber          { 0xffe8a13c };  // master / VOICE / OUT GAIN
inline const C statusGreen    { 0xff5fd08a };
inline const C panicRed       { 0xffc23b36 };
inline const C panicBorder    { 0xffd85049 };

// --- knob internals --------------------------------------------------------
inline const C arcTrack      { 0xff29323d };   // unfilled knob arc
inline const C arcGap         { 0xff0d1116 };  // the 90° bottom gap
inline const C faceTop        { 0xff2c333d };  // knob face gradient (top)
inline const C faceBottom     { 0xff141a20 };  // knob face gradient (bottom)
inline const C faceBorder     { 0xff0c1015 };
inline const C hub            { 0xff0c1015 };

// --- LCD readout -----------------------------------------------------------
inline const C lcdFill        { 0xff080b0e };
inline const C lcdBorder      { 0xff212a33 };
inline const C lcdText        { 0xffa9c2cf };  // neutral cyan default

// --- combo box -------------------------------------------------------------
inline const C comboFill      { 0xff0a0d11 };
inline const C comboBorder    { 0xff2a333c };

// --- tabs ------------------------------------------------------------------
inline const C tabInactive    { 0xff0e1116 };
inline const C tabActive      { 0xff14181d };
inline const C tabTextOff     { 0xff7b8794 };
inline const C tabTextOn      { 0xfff2f5f8 };
inline const C tabIndicator   { 0xff9aa5b2 };  // neutral grey top indicator

// Title text on a section-header bar: the dark panel/background colour (a "cut-out"
// look), not a darkened tint of the accent — reads clearly on every bright bar.
inline juce::Colour headerTextOn(juce::Colour /*accent*/)
{
    return panel;   // #0f1318 — the section-card background
}
} // namespace amyplug::colours
