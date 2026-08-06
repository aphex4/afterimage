#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

/**
    Circular Freeze control. Role is explicit (not inferred from button text).
    Geometry + label convey state for color-impaired users.
*/
class FreezeButton : public juce::ToggleButton
{
public:
    FreezeButton();
    ~FreezeButton() override = default;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    static constexpr int roleId = 0x41694672; // 'AiFr'

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreezeButton)
};
