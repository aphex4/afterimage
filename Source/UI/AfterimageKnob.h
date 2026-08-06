#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    Compact glass-dock rotary: name above, value below, precision arc drawing.
*/
class AfterimageKnob : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    AfterimageKnob();
    ~AfterimageKnob() override = default;

    void resized() override;

    void setNameLabel (const juce::String& name);
    void setValueText (const juce::String& text);
    void setTooltip (const juce::String& tip) override;
    void attachToParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId);

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    juce::String cachedValueText_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageKnob)
};
