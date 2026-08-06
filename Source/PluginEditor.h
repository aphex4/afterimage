#pragma once

#include "PluginProcessor.h"
#include "UI/AfterimageKnob.h"
#include "UI/AfterimageLookAndFeel.h"
#include "UI/MemoryWellComponent.h"
#include "UI/ModeSelector.h"
#include "UI/SpectrumDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

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
    void buildDock();
    void wireRecall();

    AfterimageAudioProcessor& audioProcessor;
    AfterimageLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label taglineLabel;
    juce::Label cpuLabel;

    ModeSelector modeSelector;
    SpectrumDisplay meterDisplay;
    MemoryWellComponent memoryWell;

    juce::ToggleButton freezeButton { "FREEZE" };
    juce::ToggleButton bypassButton { "BYPASS" };

    std::vector<std::unique_ptr<AfterimageKnob>> knobs;
    juce::Rectangle<float> dockBounds_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeParamAttachment;
    std::unique_ptr<juce::ParameterAttachment> recallParamAttachment;

    afterimage::VisualizationSnapshot snapCache_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessorEditor)
};
