#include "SpectrumDisplay.h"
#include "AfterimageLookAndFeel.h"

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (AfterimageLookAndFeel::panel().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (AfterimageLookAndFeel::glassEdge().withAlpha (0.25f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    auto meterArea = bounds.reduced (7.0f, 8.0f);

    auto drawBar = [&g] (juce::Rectangle<float> area, float level, juce::Colour colour)
    {
        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (area, 2.0f);

        const float h = area.getHeight() * juce::jlimit (0.0f, 1.0f, level);
        auto fill = area.removeFromBottom (h);
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 2.0f);
    };

    auto inArea = meterArea.removeFromLeft (meterArea.getWidth() * 0.4f);
    meterArea.removeFromLeft (meterArea.getWidth() * 0.25f);
    drawBar (inArea, inputLevel_, AfterimageLookAndFeel::accentViolet());
    drawBar (meterArea, outputLevel_, AfterimageLookAndFeel::accentCyan());
}
