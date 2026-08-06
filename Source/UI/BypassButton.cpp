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
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool bypassed = getToggleState();
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;
    const auto c = bounds.getCentre().translated (0.0f, -4.0f);

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
                      getLocalBounds().removeFromBottom (16),
                      juce::Justification::centred, 1);
}
