#include "FreezeButton.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"

FreezeButton::FreezeButton()
{
    setClickingTogglesState (true);
    setButtonText ("FREEZE");
    setTooltip (afterimage::tooltips::freeze);
    setComponentID ("freeze");
    getProperties().set ("afterimageRole", roleId);
}

void FreezeButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();

    if (on)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.16f));
        g.fillEllipse (c.x - r * 1.12f, c.y - r * 1.12f, r * 2.24f, r * 2.24f);
    }

    g.setColour (on ? AfterimageLookAndFeel::accentWarm().withAlpha (0.22f)
                    : AfterimageLookAndFeel::panel().brighter (highlighted ? 0.06f : 0.02f));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    // Geometry cue: inner ring when frozen
    if (on)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.55f));
        g.drawEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, 1.2f);
    }

    const float stroke = down ? 1.8f : (highlighted || on ? 1.4f : 1.1f);
    g.setColour (on ? AfterimageLookAndFeel::accentWarm()
                    : (highlighted ? AfterimageLookAndFeel::textPrimary().withAlpha (0.7f)
                                   : AfterimageLookAndFeel::panelEdge().brighter (0.25f)));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, stroke);

    g.setColour (on ? AfterimageLookAndFeel::accentWarm()
                    : AfterimageLookAndFeel::textMuted());
    g.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
    g.drawFittedText (on ? "HELD" : "FREEZE",
                      bounds.toNearestInt(), juce::Justification::centred, 1);
}
