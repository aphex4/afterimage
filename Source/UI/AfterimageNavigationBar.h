#pragma once

#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

/** Premium MEMORY | TUNE | EQ | FX navigation — charcoal/cyan glass, not generic TextButtons. */
class AfterimageNavigationBar : public juce::Component
{
public:
    enum class Page { Memory = 0, Tune, Eq, Fx, NumPages };

    AfterimageNavigationBar()
    {
        const char* labels[4] = { "MEMORY", "TUNE", "EQ", "FX" };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = buttons_[static_cast<size_t> (i)];
            b.setButtonText (labels[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (0xA71A);
            b.onClick = [this, i]
            {
                if (onPageChanged)
                    onPageChanged (static_cast<Page> (i));
                repaint();
            };
            addAndMakeVisible (b);
        }
        buttons_[0].setToggleState (true, juce::dontSendNotification);
    }

    std::function<void (Page)> onPageChanged;

    void setPage (Page page)
    {
        const int idx = juce::jlimit (0, 3, (int) page);
        for (int i = 0; i < 4; ++i)
            buttons_[static_cast<size_t> (i)].setToggleState (i == idx, juce::dontSendNotification);
        repaint();
    }

    [[nodiscard]] Page getPage() const noexcept
    {
        for (int i = 0; i < 4; ++i)
            if (buttons_[static_cast<size_t> (i)].getToggleState())
                return static_cast<Page> (i);
        return Page::Memory;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r.translated (0, 2), 14.0f);
        g.setColour (AfterimageLookAndFeel::glassFill());
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (AfterimageLookAndFeel::glassEdge().withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 4);
        const int w = area.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            buttons_[static_cast<size_t> (i)].setBounds (area.removeFromLeft (w).reduced (3, 2));
    }

private:
    class NavButton : public juce::TextButton
    {
    public:
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            if (on)
            {
                g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (down ? 0.28f : 0.18f));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.9f));
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
            }
            else if (highlighted)
            {
                g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.45f));
                g.fillRoundedRectangle (r, 10.0f);
            }

            g.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
            g.setColour (on ? AfterimageLookAndFeel::accentCyan()
                            : AfterimageLookAndFeel::textMuted().withAlpha (highlighted ? 0.95f : 0.8f));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    std::array<NavButton, 4> buttons_;
};
