#include "PluginEditor.h"
#include "Utilities/Constants.h"

namespace
{
void configureKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.setMouseDragSensitivity (180);
}
} // namespace

//==============================================================================
AfterimageAudioProcessorEditor::AfterimageAudioProcessorEditor (AfterimageAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (afterimage::constants::editorMinWidth,
                     afterimage::constants::editorMinHeight,
                     1600, 1200);
    setSize (afterimage::constants::editorDefaultWidth,
             afterimage::constants::editorDefaultHeight);

    titleLabel.setText ("AFTERIMAGE", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (28.0f).withStyle ("Bold"));
    titleLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    taglineLabel.setText ("EVERY SOUND LEAVES A GHOST", juce::dontSendNotification);
    taglineLabel.setFont (juce::FontOptions (11.0f));
    taglineLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    taglineLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (taglineLabel);

    addAndMakeVisible (modeSelector);
    addAndMakeVisible (meterDisplay);
    addAndMakeVisible (memoryPool);

    freezeButton.setClickingTogglesState (true);
    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (bypassButton);

    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), afterimage::constants::idFreeze, freezeButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), afterimage::constants::idBypass, bypassButton);

    if (auto* modeParam = audioProcessor.getAPVTS().getParameter (afterimage::constants::idMode))
    {
        modeParamAttachment = std::make_unique<juce::ParameterAttachment> (
            *modeParam,
            [this] (float value)
            {
                const int index = juce::roundToInt (value);
                afterimage::SpectralMode mode = afterimage::SpectralMode::Shadow;
                if (index == 1) mode = afterimage::SpectralMode::Erase;
                if (index == 2) mode = afterimage::SpectralMode::Merge;
                modeSelector.setMode (mode);
                memoryPool.setMode (mode);
            },
            nullptr);

        modeParamAttachment->sendInitialUpdate();

        modeSelector.onModeChanged = [this] (afterimage::SpectralMode mode)
        {
            modeParamAttachment->setValueAsCompleteGesture (static_cast<float> (static_cast<int> (mode)));
            memoryPool.setMode (mode);
        };
    }

    buildControls();
    startTimerHz (afterimage::constants::uiTimerHz);
}

AfterimageAudioProcessorEditor::~AfterimageAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AfterimageAudioProcessorEditor::buildControls()
{
    struct Spec { const char* name; const char* id; };
    const Spec specs[] = {
        { "MEMORY",     afterimage::constants::idMemoryLength },
        { "RECALL",     afterimage::constants::idRecallPosition },
        { "INFLUENCE",  afterimage::constants::idInfluence },
        { "FORGET",     afterimage::constants::idForget },
        { "BLUR",       afterimage::constants::idBlur },
        { "TRANSIENTS", afterimage::constants::idTransientPreserve },
        { "RANDOM",     afterimage::constants::idRandomRecall },
        { "MIX",        afterimage::constants::idMix },
        { "OUTPUT",     afterimage::constants::idOutputGain },
    };

    for (const auto& spec : specs)
    {
        auto knob = std::make_unique<Knob>();
        configureKnob (knob->slider);

        knob->nameLabel.setText (spec.name, juce::dontSendNotification);
        knob->nameLabel.setJustificationType (juce::Justification::centred);
        knob->nameLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        knob->nameLabel.setFont (juce::FontOptions (10.0f));

        knob->valueLabel.setJustificationType (juce::Justification::centred);
        knob->valueLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
        knob->valueLabel.setFont (juce::FontOptions (11.0f));

        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.getAPVTS(), spec.id, knob->slider);

        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->nameLabel);
        addAndMakeVisible (knob->valueLabel);
        knobs.push_back (std::move (knob));
    }
}

void AfterimageAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (AfterimageLookAndFeel::background().brighter (0.04f),
                             bounds.getCentreX(), bounds.getY(),
                             AfterimageLookAndFeel::background(),
                             bounds.getCentreX(), bounds.getBottom(),
                             false);
    g.setGradientFill (bg);
    g.fillAll();
}

void AfterimageAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (56);
    auto titleArea = top.removeFromLeft (280);
    titleLabel.setBounds (titleArea.removeFromTop (32));
    taglineLabel.setBounds (titleArea);

    bypassButton.setBounds (top.removeFromRight (80).reduced (0, 12));
    top.removeFromRight (8);
    meterDisplay.setBounds (top.removeFromRight (56).reduced (0, 4));
    top.removeFromRight (12);
    modeSelector.setBounds (top.removeFromRight (280).reduced (0, 10));

    auto bottom = area.removeFromBottom (150);
    area.removeFromBottom (8);

    freezeButton.setBounds (bottom.removeFromTop (36).withSizeKeepingCentre (120, 32));
    bottom.removeFromTop (8);

    const int knobCount = static_cast<int> (knobs.size());
    if (knobCount > 0)
    {
        const int knobWidth = bottom.getWidth() / knobCount;
        for (int i = 0; i < knobCount; ++i)
        {
            auto cell = bottom.removeFromLeft (knobWidth).reduced (4, 0);
            auto* knob = knobs[static_cast<std::size_t> (i)].get();
            knob->nameLabel.setBounds (cell.removeFromTop (16));
            knob->valueLabel.setBounds (cell.removeFromBottom (16));
            knob->slider.setBounds (cell.reduced (2));
        }
    }

    memoryPool.setBounds (area);
}

void AfterimageAudioProcessorEditor::timerCallback()
{
    const float inLvl = audioProcessor.getInputLevel();
    const float outLvl = audioProcessor.getOutputLevel();
    meterDisplay.setLevels (inLvl, outLvl);

    memoryPool.setInputLevel (inLvl);
    memoryPool.setOutputLevel (outLvl);
    memoryPool.setFrozen (freezeButton.getToggleState());

    if (auto* recall = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idRecallPosition))
        memoryPool.setRecallPosition (recall->load());
    if (auto* influence = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idInfluence))
        memoryPool.setInfluence (influence->load());

    static constexpr const char* ids[] = {
        afterimage::constants::idMemoryLength,
        afterimage::constants::idRecallPosition,
        afterimage::constants::idInfluence,
        afterimage::constants::idForget,
        afterimage::constants::idBlur,
        afterimage::constants::idTransientPreserve,
        afterimage::constants::idRandomRecall,
        afterimage::constants::idMix,
        afterimage::constants::idOutputGain,
    };

    for (size_t i = 0; i < knobs.size() && i < std::size (ids); ++i)
    {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ids[i]))
            knobs[i]->valueLabel.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
    }
}
