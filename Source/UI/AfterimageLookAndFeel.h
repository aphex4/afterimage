#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Flagship AFTERIMAGE look — near-black charcoal, soft cyan, muted violet.
    Glass dock surfaces, refined rotary knobs, icy Freeze control.
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

    /** Floating glass parameter dock. */
    static void paintGlassDock (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Palette
    static juce::Colour background()     { return juce::Colour (0xff080a0e); }
    static juce::Colour panel()          { return juce::Colour (0xff10141b); }
    static juce::Colour panelEdge()      { return juce::Colour (0xff1a2230); }
    static juce::Colour glassFill()      { return juce::Colour (0xcc141a24); }
    static juce::Colour glassEdge()      { return juce::Colour (0x66a8c4e0); }
    static juce::Colour textPrimary()    { return juce::Colour (0xffe8eef8); }
    static juce::Colour textMuted()      { return juce::Colour (0xff6e788c); }
    static juce::Colour accentCyan()     { return juce::Colour (0xff7ecfe0); }
    static juce::Colour accentViolet()   { return juce::Colour (0xff8577b0); }
    static juce::Colour accentWarm()     { return juce::Colour (0xffb8d4e8); } // icy, not amber
    static juce::Colour meterTrack()     { return juce::Colour (0xff161c28); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageLookAndFeel)
};
