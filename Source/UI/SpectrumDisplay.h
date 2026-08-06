#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Compact input/output level meters for the editor chrome.

    Intentionally level bars (not a full FFT spectrum view). Peak levels are
    published from the processor via VisualizationAtomics.
*/
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay() = default;

    void paint (juce::Graphics& g) override;

    void setLevels (float inputLevel, float outputLevel)
    {
        inputLevel_ = juce::jlimit (0.0f, 1.0f, inputLevel);
        outputLevel_ = juce::jlimit (0.0f, 1.0f, outputLevel);
        repaint();
    }

private:
    float inputLevel_ = 0.0f;
    float outputLevel_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
