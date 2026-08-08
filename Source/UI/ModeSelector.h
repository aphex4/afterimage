#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralModes.h"

#include <functional>

/**
    Segmented SHADOW / ERASE selector with animated selection pill.
    Continues to drive host automation via onModeChanged.
*/
class ModeSelector : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    ModeSelector();
    ~ModeSelector() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void setMode (afterimage::SpectralMode mode);
    [[nodiscard]] afterimage::SpectralMode getMode() const noexcept { return currentMode_; }

    std::function<void (afterimage::SpectralMode)> onModeChanged;

private:
    void timerCallback() override;
    [[nodiscard]] afterimage::SpectralMode modeAt (juce::Point<float> p) const noexcept;
    [[nodiscard]] juce::Rectangle<float> segmentBounds (int index) const noexcept;
    [[nodiscard]] int modeIndex (afterimage::SpectralMode mode) const noexcept;

    afterimage::SpectralMode currentMode_ = afterimage::SpectralMode::Shadow;
    float animPos_ = 0.0f; // 0..1 continuous for pill
    int hoverIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};
