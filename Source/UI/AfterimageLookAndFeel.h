#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Flagship AFTERIMAGE look: near-black charcoal, soft cyan, muted violet.
    Glass dock, precision rotaries, custom tooltips.
*/
class AfterimageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AfterimageLookAndFeel();
    ~AfterimageLookAndFeel() override = default;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawTooltip (juce::Graphics& g, const juce::String& text,
                      int width, int height) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    juce::Font getLabelFont (juce::Label& label) override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    /** Floating glass parameter dock. */
    static void paintGlassDock (juce::Graphics& g, juce::Rectangle<float> bounds);

    /** Subtle group separator inside the dock. */
    static void paintDockDivider (juce::Graphics& g, juce::Rectangle<float> bounds);

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
    static juce::Colour accentWarm()     { return juce::Colour (0xffb8d4e8); }
    static juce::Colour meterTrack()     { return juce::Colour (0xff161c28); }
    static juce::Colour tooltipBg()      { return juce::Colour (0xf0141a24); }
    static juce::Colour tooltipEdge()    { return juce::Colour (0x668a9bb5); }

private:
    static constexpr int kTooltipMaxWidth = 280;
    static constexpr int kTooltipPadX = 12;
    static constexpr int kTooltipPadY = 10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageLookAndFeel)
};
