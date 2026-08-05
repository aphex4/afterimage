#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralModes.h"

/**
    Placeholder Memory Pool visualization for Phase 1.

    Draws a dark radial field with faint arcs suggesting spectral history.
    Phase 7 replaces this with data-driven particles / trails from the DSP.
*/
class MemoryPoolComponent : public juce::Component,
                            private juce::Timer
{
public:
    MemoryPoolComponent();
    ~MemoryPoolComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setMode (afterimage::SpectralMode mode);
    void setFrozen (bool frozen);
    void setRecallPosition (float age01);
    void setInfluence (float influence01);
    void setInputLevel (float level01);
    void setOutputLevel (float level01);
    void setHistoryFill (float fill01);

private:
    void timerCallback() override;

    afterimage::SpectralMode mode_ = afterimage::SpectralMode::Shadow;
    bool frozen_ = false;
    float recallPosition_ = 0.45f;
    float influence_ = 0.5f;
    float inputLevel_ = 0.0f;
    float outputLevel_ = 0.0f;
    float historyFill_ = 0.0f;
    float phase_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryPoolComponent)
};
