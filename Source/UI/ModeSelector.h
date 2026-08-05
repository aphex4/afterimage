#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralModes.h"

#include <functional>

/**
    Three-way mode selector: SHADOW / ERASE / MERGE.
    Selected mode glows via LookAndFeel toggle colours.
*/
class ModeSelector : public juce::Component
{
public:
    ModeSelector();
    ~ModeSelector() override = default;

    void resized() override;

    void setMode (afterimage::SpectralMode mode);
    [[nodiscard]] afterimage::SpectralMode getMode() const noexcept { return currentMode_; }

    std::function<void (afterimage::SpectralMode)> onModeChanged;

    juce::TextButton shadowButton { "SHADOW" };
    juce::TextButton eraseButton  { "ERASE" };
    juce::TextButton mergeButton  { "MERGE" };

private:
    void syncToggleStates();
    void handleClick (afterimage::SpectralMode mode);

    afterimage::SpectralMode currentMode_ = afterimage::SpectralMode::Shadow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};
