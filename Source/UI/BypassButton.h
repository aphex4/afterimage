#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Compact power / bypass control. Active = processing; toggled on = bypassed.
*/
class BypassButton : public juce::ToggleButton
{
public:
    BypassButton();
    ~BypassButton() override = default;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    static constexpr int roleId = 0x41694279; // 'AiBy'

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BypassButton)
};
