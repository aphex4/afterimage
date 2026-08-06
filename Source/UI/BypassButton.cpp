#include "BypassButton.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"

BypassButton::BypassButton()
{
    setClickingTogglesState (true);
    setButtonText ("BYPASS");
    setTooltip ("BYPASS\n"
                "Smoothed pass-through of the latency-aligned dry signal.");
    setComponentID ("bypass");
    getProperties().set ("afterimageRole", roleId);
}

void BypassButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds();
    constexpr int labelH = 15;
    const auto labelArea = bounds.removeFromBottom (labelH);
    auto iconArea = bounds.toFloat().reduced (3.0f, 2.0f);

    const bool bypassed = getToggleState();
    // Keep the power glyph inside the icon strip so it never cuts the label.
    const float r = juce::jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.34f;
    const auto c = iconArea.getCentre();

    // Power glyph: arc + stem
    juce::Path power;
    power.addCentredArc (c.x, c.y, r, r, 0.0f,
                         juce::MathConstants<float>::pi * 0.28f,
                         juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi * 0.28f,
                         true);
    g.setColour (bypassed ? AfterimageLookAndFeel::accentWarm()
                          : (highlighted ? AfterimageLookAndFeel::accentCyan()
                                         : AfterimageLookAndFeel::textMuted()));
    g.strokePath (power, juce::PathStrokeType (down ? 2.0f : 1.6f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.drawLine (c.x, c.y - r * 0.15f, c.x, c.y - r * 0.95f, down ? 2.0f : 1.6f);

    g.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
    g.setColour (bypassed ? AfterimageLookAndFeel::accentWarm()
                          : AfterimageLookAndFeel::textMuted());
    g.drawFittedText (bypassed ? "BYPASSED" : "POWER",
                      labelArea,
                      juce::Justification::centred, 1);
}
