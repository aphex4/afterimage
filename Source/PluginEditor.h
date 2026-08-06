#pragma once

#include "PluginProcessor.h"
#include "UI/AfterimageKnob.h"
#include "UI/AfterimageLookAndFeel.h"
#include "UI/BypassButton.h"
#include "UI/FreezeButton.h"
#include "UI/MemoryWellComponent.h"
#include "UI/ModeSelector.h"
#include "UI/SpectrumDisplay.h"
#include "Utilities/FactoryPresets.h"

#if defined (AFTERIMAGE_ENABLE_LICENSING)
#include "UI/LicensePanel.h"
#endif

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

class AfterimageAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit AfterimageAudioProcessorEditor (AfterimageAudioProcessor&);
    ~AfterimageAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum class DockGroup { Memory, Spectral, Output };

    struct DockItem
    {
        const char* name;
        const char* id;
        const char* tip;
        DockGroup group;
    };

private:
    void timerCallback() override;
    void buildDock();
    void wireRecall();
    void buildPresetMenu();

    AfterimageAudioProcessor& audioProcessor;
    AfterimageLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 550 };

    juce::Label titleLabel;
    juce::Label memoryStatusLabel;
    juce::String cachedMemoryStatus_;
    juce::ComboBox presetBox;

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    std::unique_ptr<LicensePanel> licensePanel;
    int licenseRefreshCounter_ = 0;
#endif

    ModeSelector modeSelector;
    SpectrumDisplay meterDisplay;
    MemoryWellComponent memoryWell;

    FreezeButton freezeButton;
    BypassButton bypassButton;

    std::vector<std::unique_ptr<AfterimageKnob>> knobs;
    juce::Rectangle<float> dockBounds_;
    std::array<juce::Rectangle<float>, 2> dockDividers_ {};

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeParamAttachment;
    std::unique_ptr<juce::ParameterAttachment> recallParamAttachment;

    afterimage::VisualizationSnapshot snapCache_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessorEditor)
};
