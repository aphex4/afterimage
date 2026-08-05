#pragma once

#include "PluginProcessor.h"
#include "UI/AfterimageLookAndFeel.h"
#include "UI/MemoryPoolComponent.h"
#include "UI/ModeSelector.h"
#include "UI/SpectrumDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

class AfterimageAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit AfterimageAudioProcessorEditor (AfterimageAudioProcessor&);
    ~AfterimageAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildControls();

    AfterimageAudioProcessor& audioProcessor;
    AfterimageLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label taglineLabel;

    ModeSelector modeSelector;
    SpectrumDisplay meterDisplay;
    MemoryPoolComponent memoryPool;

    juce::ToggleButton freezeButton { "FREEZE" };
    juce::ToggleButton bypassButton { "BYPASS" };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  nameLabel;
        juce::Label  valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::vector<std::unique_ptr<Knob>> knobs;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeParamAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessorEditor)
};
