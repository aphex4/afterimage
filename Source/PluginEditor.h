#pragma once

#include "PluginProcessor.h"
#include "UI/AfterimageKnob.h"
#include "UI/AfterimageLookAndFeel.h"
#include "UI/AfterimageNavigationBar.h"
#include "UI/BypassButton.h"
#include "UI/FreezeButton.h"
#include "UI/GainMatchButton.h"
#include "UI/MemoryWellComponent.h"
#include "UI/ModeSelector.h"
#include "UI/ParametricEqPanel.h"
#include "UI/PostChainPanel.h"
#include "UI/TunePanel.h"
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

    enum class EditorView { Memory = 0, Tune, Eq, Fx };
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
    void updateModeDynamicTooltips (afterimage::SpectralMode mode);
    void setEditorView (EditorView view);
    void refreshViewVisibility();

    AfterimageAudioProcessor& audioProcessor;
    AfterimageLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 550 };

    juce::Label titleLabel;
    juce::Label memoryStatusLabel;
    juce::String cachedMemoryStatus_;
    juce::ComboBox presetBox;
    AfterimageNavigationBar navBar_;

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    std::unique_ptr<LicensePanel> licensePanel;
    int licenseRefreshCounter_ = 0;
#endif

    ModeSelector modeSelector;
    SpectrumDisplay meterDisplay;
    MemoryWellComponent memoryWell;
    PostChainPanel postChainPanel;
    TunePanel tunePanel;
    ParametricEqPanel eqPanel;

    FreezeButton freezeButton;
    GainMatchButton gainMatchButton;
    BypassButton bypassButton;

    std::vector<std::unique_ptr<AfterimageKnob>> knobs;
    juce::Rectangle<float> dockBounds_;
    std::array<juce::Rectangle<float>, 2> dockDividers_ {};
    juce::Rectangle<float> navBarBounds_;
    EditorView editorView_ = EditorView::Memory;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gainMatchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeParamAttachment;
    std::unique_ptr<juce::ParameterAttachment> recallParamAttachment;

    afterimage::VisualizationSnapshot snapCache_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessorEditor)
};
