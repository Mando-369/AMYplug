// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace amyplug
{
// The single component that carries the UI-scale AffineTransform.
//
// Every control an editor shows is a child of this, laid out at the DESIGN size — so no
// layout code anywhere knows that scaling exists. The editor only ever computes a scale
// factor and hands it over.
//
// It also gives popup menus something to parent themselves to. That matters: PopupMenu's
// own auto-scale path (`shouldPopupMenuScaleWithTargetComponent`) is skipped entirely when
// `options.getParentComponent() != nullptr`, so a menu parented to the *editor* would come
// up at 100% over a 150% panel. Parenting to this instead makes the menu a child of the
// transform, so it scales with everything else. See
// Code Repo/JUCE-UI-LnF__15 (the ⚠️ on withParentComponent) and __13 (the size picker).
class ScaledContent final : public juce::Component
{
public:
    ScaledContent(std::function<void (juce::Graphics&)> paintFn,
                  std::function<void()> layoutFn)
        : paintContent(std::move(paintFn)), layoutContent(std::move(layoutFn))
    {
        setInterceptsMouseClicks(false, true);   // a pass-through frame, not a control
    }

    void paint(juce::Graphics& g) override { if (paintContent) paintContent(g); }
    void resized() override                { if (layoutContent) layoutContent(); }

    // Fit the design surface into `outer` and apply the scale. Returns the whole percent
    // applied, which is what gets stored and shown — see AmyPlugEditor::resized.
    int fitInto(int baseWidth, int baseHeight, juce::Rectangle<int> outer)
    {
        const float scale = juce::jmin((float) outer.getWidth()  / (float) baseWidth,
                                       (float) outer.getHeight() / (float) baseHeight);
        setBounds(0, 0, baseWidth, baseHeight);          // always the design size
        setTransform(juce::AffineTransform::scale(scale));
        return juce::roundToInt(scale * 100.0f);
    }

private:
    std::function<void (juce::Graphics&)> paintContent;
    std::function<void()>                 layoutContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaledContent)
};
} // namespace amyplug
