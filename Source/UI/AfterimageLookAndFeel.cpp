#include "AfterimageLookAndFeel.h"

AfterimageLookAndFeel::AfterimageLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background());
    setColour (juce::Slider::rotarySliderFillColourId, accentCyan());
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge());
    setColour (juce::Slider::thumbColourId, textPrimary());
    setColour (juce::Label::textColourId, textPrimary());
    setColour (juce::TextButton::buttonColourId, panel());
    setColour (juce::TextButton::buttonOnColourId, accentViolet().withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, textMuted());
    setColour (juce::TextButton::textColourOnId, accentCyan());
    setColour (juce::ToggleButton::textColourId, textPrimary());
    setColour (juce::ToggleButton::tickColourId, accentCyan());
    setColour (juce::ToggleButton::tickDisabledColourId, textMuted());
}

void AfterimageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle,
                                              float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                static_cast<float> (y),
                                                static_cast<float> (width),
                                                static_cast<float> (height)).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW = juce::jmax (2.0f, radius * 0.08f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (panelEdge());
    g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, toAngle, true);

        const bool hovered = slider.isMouseOverOrDragging();
        g.setColour (accentCyan().withAlpha (hovered ? 1.0f : 0.85f));
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        juce::Point<float> thumb (centre.x + radius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                  centre.y + radius * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        g.setColour (textPrimary().withAlpha (0.9f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (thumb));
    }
}

void AfterimageLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? accentWarm().withAlpha (0.25f) : panel());
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (on ? accentWarm() : (shouldDrawButtonAsHighlighted ? accentCyan() : panelEdge()));
    g.drawRoundedRectangle (bounds, 6.0f, shouldDrawButtonAsDown ? 1.5f : 1.0f);

    g.setColour (on ? accentWarm() : textMuted());
    g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1);
}

void AfterimageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*backgroundColour*/,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? accentViolet().withAlpha (0.4f)
                    : panel().brighter (shouldDrawButtonAsHighlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (on ? accentCyan() : panelEdge().brighter (shouldDrawButtonAsDown ? 0.2f : 0.0f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

juce::Font AfterimageLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::FontOptions (11.0f);
}

juce::Font AfterimageLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (12.0f).withStyle ("Bold");
}
