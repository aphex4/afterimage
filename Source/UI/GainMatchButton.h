#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Circular Gain Match control. Matches Freeze geometry language;
    cyan accent when compensating wet level toward dry.
*/
class GainMatchButton : public juce::ToggleButton
{
public:
    GainMatchButton();
    ~GainMatchButton() override = default;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    static constexpr int roleId = 0x41694d61; // 'AiMa'

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainMatchButton)
};
