#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Dark, restrained AFTERIMAGE look — pale cyan / muted violet accents on
    near-black charcoal. No OpenGL in Phase 1.
*/
class AfterimageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AfterimageLookAndFeel();
    ~AfterimageLookAndFeel() override = default;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label& label) override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;

    // Palette
    static juce::Colour background()     { return juce::Colour (0xff0b0d12); }
    static juce::Colour panel()          { return juce::Colour (0xff12151c); }
    static juce::Colour panelEdge()      { return juce::Colour (0xff1c2230); }
    static juce::Colour textPrimary()    { return juce::Colour (0xffe8eef8); }
    static juce::Colour textMuted()      { return juce::Colour (0xff7a8499); }
    static juce::Colour accentCyan()     { return juce::Colour (0xff7fd4e8); }
    static juce::Colour accentViolet()   { return juce::Colour (0xff8b7bb8); }
    static juce::Colour accentWarm()     { return juce::Colour (0xffd4a574); }
    static juce::Colour meterTrack()     { return juce::Colour (0xff1a2030); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageLookAndFeel)
};
