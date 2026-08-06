#include "AfterimageLookAndFeel.h"
#include "AfterimageFonts.h"

#include <cmath>

AfterimageLookAndFeel::AfterimageLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background());
    setColour (juce::Slider::rotarySliderFillColourId, accentCyan());
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge());
    setColour (juce::Slider::thumbColourId, textPrimary());
    setColour (juce::Label::textColourId, textPrimary());
    setColour (juce::ComboBox::backgroundColourId, panel());
    setColour (juce::ComboBox::outlineColourId, panelEdge());
    setColour (juce::ComboBox::textColourId, textPrimary());
    setColour (juce::ComboBox::arrowColourId, textMuted());
    setColour (juce::PopupMenu::backgroundColourId, panel());
    setColour (juce::PopupMenu::textColourId, textPrimary());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentViolet().withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, textPrimary());
    setColour (juce::TooltipWindow::backgroundColourId, tooltipBg());
    setColour (juce::TooltipWindow::textColourId, textPrimary());
    setColour (juce::TooltipWindow::outlineColourId, tooltipEdge());
}

void AfterimageLookAndFeel::paintGlassDock (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), 16.0f);

    juce::ColourGradient gloss (panel().brighter (0.06f).withAlpha (0.55f),
                                bounds.getX(), bounds.getY(),
                                glassFill(),
                                bounds.getX(), bounds.getBottom(),
                                false);
    g.setGradientFill (gloss);
    g.fillRoundedRectangle (bounds, 16.0f);

    g.setColour (glassEdge().withAlpha (0.32f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 16.0f, 1.0f);
}

void AfterimageLookAndFeel::paintDockDivider (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float x = bounds.getCentreX();
    g.setColour (panelEdge().brighter (0.15f).withAlpha (0.55f));
    g.drawLine (x, bounds.getY() + 10.0f, x, bounds.getBottom() - 10.0f, 1.0f);
}

void AfterimageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle,
                                              float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                (float) width, (float) height).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.46f;
    const auto centre = bounds.getCentre();
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const bool active = slider.isMouseOverOrDragging();
    const float lineW = juce::jmax (1.6f, radius * 0.07f);

    // Thin inactive arc
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (panelEdge().brighter (0.08f).withAlpha (0.9f));
    g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    if (! slider.isEnabled())
        return;

    // Controlled active arc
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, toAngle, true);
    g.setColour (accentCyan().withAlpha (active ? 0.95f : 0.72f));
    g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // Minimal indicator notch (no center-to-edge needle)
    const float notchR = radius;
    const float ang = toAngle - juce::MathConstants<float>::halfPi;
    const juce::Point<float> tip (centre.x + notchR * std::cos (ang),
                                  centre.y + notchR * std::sin (ang));
    g.setColour (textPrimary().withAlpha (active ? 0.95f : 0.7f));
    g.fillEllipse (juce::Rectangle<float> (active ? 4.0f : 3.2f,
                                           active ? 4.0f : 3.2f).withCentre (tip));

    // Quiet hub
    g.setColour (panelEdge().brighter (0.12f));
    g.fillEllipse (centre.x - 2.2f, centre.y - 2.2f, 4.4f, 4.4f);
}

juce::Rectangle<int> AfterimageLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                              juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
{
    const auto titleFont = AfterimageFonts::get (AfterimageFontRole::TooltipTitle);
    const auto bodyFont = AfterimageFonts::get (AfterimageFontRole::TooltipBody);

    juce::String title, body;
    const int nl = tipText.indexOfChar ('\n');
    if (nl >= 0)
    {
        title = tipText.substring (0, nl).trim();
        body = tipText.substring (nl + 1).trim();
    }
    else
    {
        body = tipText.trim();
    }

    const int maxTextW = kTooltipMaxWidth - kTooltipPadX * 2;
    int textH = 0;
    int textW = 0;

    if (title.isNotEmpty())
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (titleFont, title, 0.0f, 0.0f);
        textW = juce::jmax (textW, juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth()));
        textH += juce::roundToInt (std::ceil (titleFont.getHeight()));
        if (body.isNotEmpty())
            textH += kTooltipTitleBodyGap;
    }

    if (body.isNotEmpty())
    {
        juce::AttributedString as;
        as.setJustification (juce::Justification::topLeft);
        as.append (body, bodyFont, textPrimary().withAlpha (0.88f));
        juce::TextLayout layout;
        layout.createLayout (as, (float) maxTextW);
        textW = juce::jmax (textW, juce::jmin (maxTextW, juce::roundToInt (std::ceil (layout.getWidth()))));
        // Ceil + 1px slack so last line never sits on the border.
        textH += juce::roundToInt (std::ceil (layout.getHeight())) + 1;
    }

    const int w = juce::jlimit (128, kTooltipMaxWidth, textW + kTooltipPadX * 2);
    const int h = textH + kTooltipPadY * 2;

    int x = screenPos.x + 14;
    int y = screenPos.y + 18;
    if (x + w > parentArea.getRight())
        x = juce::jmax (parentArea.getX(), parentArea.getRight() - w);
    if (y + h > parentArea.getBottom())
        y = juce::jmax (parentArea.getY(), screenPos.y - h - 8);

    return { x, y, w, h };
}

void AfterimageLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                          int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 8.0f);

    g.setColour (tooltipBg());
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (tooltipEdge());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Inset past the 1px stroke so text never touches the edge.
    auto inner = bounds.reduced ((float) kTooltipPadX, (float) kTooltipPadY);

    juce::String title, body;
    const int nl = text.indexOfChar ('\n');
    if (nl >= 0)
    {
        title = text.substring (0, nl).trim();
        body = text.substring (nl + 1).trim();
    }
    else
    {
        body = text.trim();
    }

    if (title.isNotEmpty())
    {
        const float titleH = AfterimageFonts::height (AfterimageFontRole::TooltipTitle);
        g.setFont (AfterimageFonts::get (AfterimageFontRole::TooltipTitle));
        g.setColour (accentCyan().withAlpha (0.9f));
        g.drawText (title, inner.removeFromTop (titleH).toNearestInt(),
                    juce::Justification::centredLeft, false);
        if (body.isNotEmpty())
            inner.removeFromTop ((float) kTooltipTitleBodyGap);
    }

    if (body.isNotEmpty())
    {
        juce::AttributedString as;
        as.setJustification (juce::Justification::topLeft);
        as.append (body, AfterimageFonts::get (AfterimageFontRole::TooltipBody),
                   textPrimary().withAlpha (0.88f));
        juce::TextLayout layout;
        layout.createLayout (as, inner.getWidth());
        layout.draw (g, inner);
    }
}

juce::Font AfterimageLookAndFeel::getLabelFont (juce::Label&)
{
    return AfterimageFonts::get (AfterimageFontRole::Status);
}

juce::Font AfterimageLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return AfterimageFonts::get (AfterimageFontRole::Mode);
}

juce::Font AfterimageLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return AfterimageFonts::get (AfterimageFontRole::Status);
}

void AfterimageLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (panel().brighter (box.isMouseOver() ? 0.06f : 0.02f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (panelEdge().brighter (0.1f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const float arrowX = (float) width - 14.0f;
    const float arrowY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowX - 4.0f, arrowY - 2.0f,
                       arrowX + 4.0f, arrowY - 2.0f,
                       arrowX, arrowY + 3.0f);
    g.setColour (textMuted());
    g.fillPath (arrow);
}

void AfterimageLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (panel());
    g.fillRect (0, 0, width, height);
    g.setColour (panelEdge());
    g.drawRect (0, 0, width, height, 1);
}

void AfterimageLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                               bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool /*hasSubMenu*/,
                                               const juce::String& text,
                                               const juce::String& /*shortcutKeyText*/,
                                               const juce::Drawable* /*icon*/,
                                               const juce::Colour* /*textColourToUse*/)
{
    if (isSeparator)
    {
        g.setColour (panelEdge());
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (accentViolet().withAlpha (0.3f));
        g.fillRect (area.reduced (2, 1));
    }

    g.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
    g.setColour (isActive ? textPrimary() : textMuted());
    auto textArea = area.reduced (10, 0);
    if (isTicked)
    {
        g.setColour (accentCyan());
        g.fillEllipse ((float) textArea.getX(), (float) area.getCentreY() - 2.5f, 5.0f, 5.0f);
        textArea.removeFromLeft (12);
    }
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);
}
