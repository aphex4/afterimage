#include "PluginEditor.h"
#include "Utilities/Constants.h"

namespace
{
struct DockSpec
{
    const char* name;
    const char* id;
    const char* tip;
};

const DockSpec kDockSpecs[] = {
    { "MEMORY",    afterimage::constants::idMemoryLength,
      "Memory Length — how far back the searchable spectral history extends (0.1–10 s)." },
    { "INFLUENCE", afterimage::constants::idInfluence,
      "Influence — strength of spectral interaction with recalled memory. 0% = transparent." },
    { "FORGET",    afterimage::constants::idForget,
      "Forget — how quickly older recalled frames lose weight." },
    { "BLUR",      afterimage::constants::idBlur,
      "Blur — smooth history magnitudes across neighboring frequency bins." },
    { "TRANSIENT", afterimage::constants::idTransientPreserve,
      "Transients — preserve attacks by reducing influence when transients are detected." },
    { "RANDOM",    afterimage::constants::idRandomRecall,
      "Random Recall — slow smoothed wander around Recall Position (not chaotic per-hop jumps)." },
    { "MIX",       afterimage::constants::idMix,
      "Mix — equal-power dry/wet blend (dry is latency-aligned with the STFT)." },
    { "OUTPUT",    afterimage::constants::idOutputGain,
      "Output — final gain trim after mix and bypass (−24…+12 dB)." },
};
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
                     1800, 1400);
    setSize (afterimage::constants::editorDefaultWidth,
             afterimage::constants::editorDefaultHeight);

    titleLabel.setText ("AFTERIMAGE", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (30.0f).withStyle ("Bold"));
    titleLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    taglineLabel.setText ("EVERY SOUND LEAVES A GHOST", juce::dontSendNotification);
    taglineLabel.setFont (juce::FontOptions (10.5f));
    taglineLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    taglineLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (taglineLabel);

    cpuLabel.setText ("CPU —", juce::dontSendNotification);
    cpuLabel.setFont (juce::FontOptions (10.0f));
    cpuLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);

    buildPresetMenu();

    addAndMakeVisible (modeSelector);
    addAndMakeVisible (meterDisplay);
    addAndMakeVisible (memoryWell);
    memoryWell.setTooltip ("Memory Well — scrub the Recall ring to choose history age (outer = newest).");

    freezeButton.setClickingTogglesState (true);
    bypassButton.setClickingTogglesState (true);
    freezeButton.setTooltip ("Freeze — stop writing new spectral frames; keep recalling frozen memory.");
    bypassButton.setTooltip ("Bypass — smoothed pass-through of latency-aligned dry signal.");
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
                memoryWell.setMode (mode);
            },
            nullptr);

        modeParamAttachment->sendInitialUpdate();

        modeSelector.onModeChanged = [this] (afterimage::SpectralMode mode)
        {
            modeParamAttachment->setValueAsCompleteGesture (static_cast<float> (static_cast<int> (mode)));
            memoryWell.setMode (mode);
        };
    }

    wireRecall();
    buildDock();

    // setSize() ran before dock knobs existed — lay them out now so they
    // appear at the default window size without requiring a resize.
    resized();

    startTimerHz (afterimage::constants::uiTimerHz);
}

AfterimageAudioProcessorEditor::~AfterimageAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AfterimageAudioProcessorEditor::buildPresetMenu()
{
    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.setTooltip ("Factory presets — parameter starting points only (history is cleared on load).");
    for (int i = 0; i < afterimage::factory::kNumPresets; ++i)
        presetBox.addItem (afterimage::factory::kPresets[static_cast<std::size_t> (i)].name, i + 1);

    presetBox.setSelectedId (audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0)
            audioProcessor.setCurrentProgram (id - 1);
    };
    addAndMakeVisible (presetBox);
}

void AfterimageAudioProcessorEditor::wireRecall()
{
    if (auto* recallParam = audioProcessor.getAPVTS().getParameter (afterimage::constants::idRecallPosition))
    {
        recallParamAttachment = std::make_unique<juce::ParameterAttachment> (
            *recallParam,
            [this] (float value)
            {
                memoryWell.setRecallPosition (value);
            },
            nullptr);

        recallParamAttachment->sendInitialUpdate();

        memoryWell.onRecallGestureStart = [this]
        {
            if (recallParamAttachment)
                recallParamAttachment->beginGesture();
        };

        memoryWell.onRecallChanged = [this] (float age01)
        {
            if (recallParamAttachment)
                recallParamAttachment->setValueAsPartOfGesture (age01);
        };

        memoryWell.onRecallGestureEnd = [this]
        {
            if (recallParamAttachment)
                recallParamAttachment->endGesture();
        };
    }
}

void AfterimageAudioProcessorEditor::buildDock()
{
    // Recall lives on the Memory Well ring — not in the dock.
    for (const auto& spec : kDockSpecs)
    {
        auto knob = std::make_unique<AfterimageKnob>();
        knob->setNameLabel (spec.name);
        knob->setTooltip (spec.tip);
        knob->attachToParameter (audioProcessor.getAPVTS(), spec.id);
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }
}

void AfterimageAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (AfterimageLookAndFeel::background().brighter (0.05f),
                             bounds.getCentreX(), bounds.getY(),
                             AfterimageLookAndFeel::background().darker (0.15f),
                             bounds.getCentreX(), bounds.getBottom(),
                             false);
    g.setGradientFill (bg);
    g.fillAll();

    // Soft vignette toward edges
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (bounds.removeFromLeft (28.0f));
    bounds = getLocalBounds().toFloat();
    g.fillRect (bounds.removeFromRight (28.0f));

    if (! dockBounds_.isEmpty())
        AfterimageLookAndFeel::paintGlassDock (g, dockBounds_);
}

void AfterimageAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    // Header
    auto top = area.removeFromTop (58);
    auto titleArea = top.removeFromLeft (260);
    titleLabel.setBounds (titleArea.removeFromTop (34));
    taglineLabel.setBounds (titleArea);

    bypassButton.setBounds (top.removeFromRight (78).reduced (0, 14));
    top.removeFromRight (10);
    meterDisplay.setBounds (top.removeFromRight (52).reduced (0, 6));
    top.removeFromRight (8);
    cpuLabel.setBounds (top.removeFromRight (64).reduced (0, 16));
    top.removeFromRight (12);
    modeSelector.setBounds (top.removeFromRight (292).reduced (0, 12));
    top.removeFromRight (10);
    presetBox.setBounds (top.removeFromRight (168).reduced (0, 14));

    // Bottom glass dock
    auto dock = area.removeFromBottom (128);
    area.removeFromBottom (6);
    dockBounds_ = dock.toFloat();
    auto dockInner = dock.reduced (16, 10);

    // Freeze sits centered under the well, above the dock
    auto freezeRow = area.removeFromBottom (72);
    freezeButton.setBounds (freezeRow.withSizeKeepingCentre (72, 72));
    area.removeFromBottom (4);

    // Memory Well fills remaining centre
    memoryWell.setBounds (area);

    const int knobCount = static_cast<int> (knobs.size());
    if (knobCount > 0)
    {
        const int knobWidth = dockInner.getWidth() / knobCount;
        for (int i = 0; i < knobCount; ++i)
        {
            auto cell = dockInner.removeFromLeft (knobWidth).reduced (3, 0);
            knobs[static_cast<std::size_t> (i)]->setBounds (cell);
        }
    }
}

void AfterimageAudioProcessorEditor::timerCallback()
{
    const float inLvl = audioProcessor.getInputLevel();
    const float outLvl = audioProcessor.getOutputLevel();
    meterDisplay.setLevels (inLvl, outLvl);

    audioProcessor.getEngine().getSnapshotPublisher().copyLatest (snapCache_);
    memoryWell.applySnapshot (snapCache_);

    if (auto* influence = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idInfluence))
        memoryWell.setInfluence (influence->load());
    if (auto* memory = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idMemoryLength))
    {
        // Normalise 0.1..10s roughly for well feel (APVTS stores seconds as raw)
        const float sec = memory->load();
        const float norm = juce::jlimit (0.0f, 1.0f,
            (sec - afterimage::constants::memoryLengthMinSec)
                / (afterimage::constants::memoryLengthMaxSec - afterimage::constants::memoryLengthMinSec));
        memoryWell.setMemoryLengthNorm (norm);
    }

    // Lightweight CPU hint from host (when available)
    if (auto* playHead = audioProcessor.getPlayHead())
    {
        // No reliable CPU meter from playhead — show activity instead
        juce::ignoreUnused (playHead);
    }
    const int activity = juce::roundToInt (snapCache_.historyFill * 100.0f);
    cpuLabel.setText ("MEM " + juce::String (activity) + "%", juce::dontSendNotification);

    // Keep preset box in sync if host/program API changed selection
    const int wantId = audioProcessor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != wantId)
        presetBox.setSelectedId (wantId, juce::dontSendNotification);

    for (size_t i = 0; i < knobs.size() && i < std::size (kDockSpecs); ++i)
    {
        if (auto* param = audioProcessor.getAPVTS().getParameter (kDockSpecs[i].id))
            knobs[i]->setValueText (param->getCurrentValueAsText());
    }
}
