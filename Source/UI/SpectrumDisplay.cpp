#include "SpectrumDisplay.h"
#include "AfterimageLookAndFeel.h"

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (AfterimageLookAndFeel::panel());
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (AfterimageLookAndFeel::panelEdge());
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    auto meterArea = bounds.reduced (6.0f);
    const float mid = meterArea.getCentreX();

    auto drawBar = [&g] (juce::Rectangle<float> area, float level, juce::Colour colour)
    {
        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (area, 2.0f);

        auto fill = area.removeFromBottom (area.getHeight() * level);
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 2.0f);
    };

    drawBar (meterArea.removeFromLeft (meterArea.getWidth() * 0.42f),
             inputLevel_, AfterimageLookAndFeel::accentViolet());
    meterArea.removeFromLeft (meterArea.getWidth() * 0.2f);
    juce::ignoreUnused (mid);
    drawBar (meterArea, outputLevel_, AfterimageLookAndFeel::accentCyan());
}
