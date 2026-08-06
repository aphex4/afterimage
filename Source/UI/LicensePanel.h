#pragma once

#include "../Licensing/LicenseManager.h"
#include "AfterimageLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
    Compact header license chip + AFTERIMAGE-styled activation modal.
    Does not clutter the main creative UI.
*/
class LicensePanel : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit LicensePanel (afterimage::licensing::LicenseManager& manager);
    ~LicensePanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    void refreshStatus();

    std::function<void()> onStatusChanged;

private:
    class ActivationOverlay;

    void openOverlay();
    void closeOverlay();

    afterimage::licensing::LicenseManager& manager_;
    juce::String chipText_;
    juce::Colour chipColour_ { AfterimageLookAndFeel::textMuted() };
    std::unique_ptr<ActivationOverlay> overlay_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicensePanel)
};
