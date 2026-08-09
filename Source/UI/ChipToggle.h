#pragma once

#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Premium charcoal/cyan chip toggle — replaces default JUCE checkboxes in main UI. */
class ChipToggle : public juce::ToggleButton
{
public:
    ChipToggle()
    {
        setClickingTogglesState (true);
        setColour (juce::ToggleButton::textColourId, AfterimageLookAndFeel::textPrimary());
        setColour (juce::ToggleButton::tickColourId, AfterimageLookAndFeel::accentCyan());
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        const float alpha = down ? 0.95f : (highlighted ? 0.85f : 0.75f);

        g.setColour (AfterimageLookAndFeel::panel().withAlpha (0.95f));
        g.fillRoundedRectangle (r, 8.0f);

        if (on)
        {
            g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.22f * alpha));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.85f));
        }
        else
        {
            g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.8f));
        }
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);

        g.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
        g.setColour (on ? AfterimageLookAndFeel::accentCyan()
                        : AfterimageLookAndFeel::textMuted().withAlpha (alpha));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};
