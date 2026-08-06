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

void AfterimageLookAndFeel::paintGlassDock (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), 18.0f);

    juce::ColourGradient gloss (panel().brighter (0.06f).withAlpha (0.55f),
                                bounds.getX(), bounds.getY(),
                                glassFill(),
                                bounds.getX(), bounds.getBottom(),
                                false);
    g.setGradientFill (gloss);
    g.fillRoundedRectangle (bounds, 18.0f);

    g.setColour (glassEdge().withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 18.0f, 1.0f);

    // Top highlight edge
    g.setColour (textPrimary().withAlpha (0.06f));
    g.drawLine (bounds.getX() + 20.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 20.0f, bounds.getY() + 1.0f, 1.0f);
}

void AfterimageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle,
                                              float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                static_cast<float> (y),
                                                static_cast<float> (width),
                                                static_cast<float> (height)).reduced (5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.48f;
    const auto centre = bounds.getCentre();
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW = juce::jmax (2.0f, radius * 0.09f);

    // Soft body disc
    g.setColour (panel().brighter (0.03f));
    g.fillEllipse (centre.x - radius * 0.72f, centre.y - radius * 0.72f,
                   radius * 1.44f, radius * 1.44f);

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
        const auto fill = accentCyan().interpolatedWith (accentViolet(), 0.2f);
        g.setColour (fill.withAlpha (hovered ? 1.0f : 0.82f));
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Needle
        const float needleLen = radius * 0.62f;
        const juce::Point<float> tip (centre.x + needleLen * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                      centre.y + needleLen * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        g.setColour (textPrimary().withAlpha (0.85f));
        g.drawLine (centre.x, centre.y, tip.x, tip.y, 1.4f);
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (tip));

        // Hub
        g.setColour (panelEdge().brighter (0.15f));
        g.fillEllipse (centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
}

void AfterimageLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = button.getToggleState();
    const bool freeze = button.getButtonText().containsIgnoreCase ("FREEZE");

    if (freeze)
    {
        // Circular icy Freeze control
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();

        if (on)
        {
            g.setColour (accentWarm().withAlpha (0.18f));
            g.fillEllipse (c.x - r * 1.15f, c.y - r * 1.15f, r * 2.3f, r * 2.3f);
        }

        g.setColour (on ? accentWarm().withAlpha (0.28f) : panel().brighter (0.04f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        g.setColour (on ? accentWarm()
                        : (shouldDrawButtonAsHighlighted ? accentCyan() : panelEdge().brighter (0.2f)));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, shouldDrawButtonAsDown ? 1.8f : 1.2f);

        g.setColour (on ? accentWarm() : textMuted());
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    g.setColour (on ? accentWarm().withAlpha (0.22f) : panel());
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (on ? accentWarm() : (shouldDrawButtonAsHighlighted ? accentCyan() : panelEdge()));
    g.drawRoundedRectangle (bounds, 6.0f, shouldDrawButtonAsDown ? 1.5f : 1.0f);

    g.setColour (on ? accentWarm() : textMuted());
    g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1);
}

void AfterimageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*backgroundColour*/,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? accentViolet().withAlpha (0.38f)
                    : panel().brighter (shouldDrawButtonAsHighlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? accentCyan() : panelEdge().brighter (shouldDrawButtonAsDown ? 0.25f : 0.0f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

juce::Font AfterimageLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::FontOptions (11.0f);
}

juce::Font AfterimageLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (11.5f).withStyle ("Bold");
}
