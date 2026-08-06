#include "ModeSelector.h"

ModeSelector::ModeSelector()
{
    auto setup = [this] (juce::TextButton& b, afterimage::SpectralMode mode)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.setEnabled (true);
        addAndMakeVisible (b);
        b.onClick = [this, mode]
        {
            handleClick (mode);
        };
    };

    setup (shadowButton, afterimage::SpectralMode::Shadow);
    setup (eraseButton,  afterimage::SpectralMode::Erase);
    setup (mergeButton,  afterimage::SpectralMode::Merge);

    shadowButton.setTooltip ("Shadow — additive spectral ghost of recalled memory");
    eraseButton.setTooltip ("Erase — carve holes where memory overlaps the present");
    mergeButton.setTooltip ("Merge — morph the present toward recalled memory");

    syncToggleStates();
}

void ModeSelector::resized()
{
    auto r = getLocalBounds();
    const int gap = 4;
    const int w = (r.getWidth() - gap * 2) / 3;
    shadowButton.setBounds (r.removeFromLeft (w));
    r.removeFromLeft (gap);
    eraseButton.setBounds (r.removeFromLeft (w));
    r.removeFromLeft (gap);
    mergeButton.setBounds (r);
}

void ModeSelector::setMode (afterimage::SpectralMode mode)
{
    currentMode_ = mode;
    syncToggleStates();
}

void ModeSelector::syncToggleStates()
{
    shadowButton.setToggleState (currentMode_ == afterimage::SpectralMode::Shadow, juce::dontSendNotification);
    eraseButton.setToggleState  (currentMode_ == afterimage::SpectralMode::Erase,  juce::dontSendNotification);
    mergeButton.setToggleState  (currentMode_ == afterimage::SpectralMode::Merge,  juce::dontSendNotification);
}

void ModeSelector::handleClick (afterimage::SpectralMode mode)
{
    currentMode_ = mode;
    syncToggleStates();
    if (onModeChanged)
        onModeChanged (mode);
}
