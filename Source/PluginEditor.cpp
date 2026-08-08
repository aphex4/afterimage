#include "PluginEditor.h"
#include "UI/AfterimageFonts.h"
#include "UI/AfterimageTooltips.h"
#include "Utilities/Constants.h"

namespace
{
const AfterimageAudioProcessorEditor::DockItem* dockItems()
{
    // Tips use ASCII only (no em/en dashes, no smart quotes).
    // Influence tip is mode-dynamic; dock default is replaced in buildDock / mode updates.
    static const AfterimageAudioProcessorEditor::DockItem items[] = {
        { "MEMORY", afterimage::constants::idMemoryLength,
          afterimage::tooltips::memory,
          AfterimageAudioProcessorEditor::DockGroup::Memory },
        { "FORGET", afterimage::constants::idForget,
          afterimage::tooltips::forget,
          AfterimageAudioProcessorEditor::DockGroup::Memory },
        { "INFLUENCE", afterimage::constants::idInfluence,
          afterimage::tooltips::influenceShadow,
          AfterimageAudioProcessorEditor::DockGroup::Spectral },
        { "BLUR", afterimage::constants::idBlur,
          afterimage::tooltips::blurShadow,
          AfterimageAudioProcessorEditor::DockGroup::Spectral },
        { "TRANSIENT", afterimage::constants::idTransientPreserve,
          afterimage::tooltips::transientPreserve,
          AfterimageAudioProcessorEditor::DockGroup::Spectral },
        { "RANDOM", afterimage::constants::idRandomRecall,
          afterimage::tooltips::random,
          AfterimageAudioProcessorEditor::DockGroup::Spectral },
        { "MIX", afterimage::constants::idMix,
          afterimage::tooltips::mix,
          AfterimageAudioProcessorEditor::DockGroup::Output },
        { "OUTPUT", afterimage::constants::idOutputGain,
          afterimage::tooltips::output,
          AfterimageAudioProcessorEditor::DockGroup::Output },
    };
    return items;
}

constexpr int kInfluenceKnobIndex = 2;
constexpr int kBlurKnobIndex = 3;
constexpr int kDockCount = 8;
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
    titleLabel.setFont (AfterimageFonts::get (AfterimageFontRole::Wordmark));
    titleLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setTooltip (afterimage::tooltips::afterimage);
    addAndMakeVisible (titleLabel);

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    licensePanel = std::make_unique<LicensePanel> (audioProcessor.getLicenseManager());
    addAndMakeVisible (*licensePanel);
#endif

    memoryStatusLabel.setText ("FILL 0%", juce::dontSendNotification);
    memoryStatusLabel.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
    memoryStatusLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    memoryStatusLabel.setJustificationType (juce::Justification::centredLeft);
    // Keep mouse hits so the fill tooltip can show (not a meter label).
    memoryStatusLabel.setTooltip (afterimage::tooltips::memoryStatus);
    addAndMakeVisible (memoryStatusLabel);

    buildPresetMenu();

    addAndMakeVisible (modeSelector);
    addAndMakeVisible (meterDisplay);
    addAndMakeVisible (memoryWell);

    addAndMakeVisible (freezeButton);
    addAndMakeVisible (gainMatchButton);
    addAndMakeVisible (bypassButton);

    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), afterimage::constants::idFreeze, freezeButton);
    gainMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), afterimage::constants::idGainMatch, gainMatchButton);
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
                updateModeDynamicTooltips (mode);
            },
            nullptr);

        modeParamAttachment->sendInitialUpdate();
        updateModeDynamicTooltips (modeSelector.getMode());

        modeSelector.onModeChanged = [this] (afterimage::SpectralMode mode)
        {
            modeParamAttachment->setValueAsCompleteGesture (static_cast<float> (static_cast<int> (mode)));
            memoryWell.setMode (mode);
            updateModeDynamicTooltips (mode);
        };
    }

    wireRecall();
    buildDock();
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
    presetBox.setTextWhenNothingSelected ("Custom");
    presetBox.setTooltip (afterimage::tooltips::preset);

    int lastMode = -1;
    for (int i = 0; i < afterimage::factory::kNumPresets; ++i)
    {
        const auto& pr = afterimage::factory::kPresets[static_cast<std::size_t> (i)];
        if (pr.mode != lastMode)
        {
            if (lastMode >= 0)
                presetBox.addSeparator();

            const char* heading = "Shadow";
            if (pr.mode == 1) heading = "Erase";
            if (pr.mode == 2) heading = "Merge";
            presetBox.addSectionHeading (heading);
            lastMode = pr.mode;
        }

        presetBox.addItem (pr.name, i + 1);
    }

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
    const auto* specs = dockItems();
    for (int i = 0; i < kDockCount; ++i)
    {
        auto knob = std::make_unique<AfterimageKnob>();
        knob->setNameLabel (specs[i].name);
        knob->setTooltip (specs[i].tip);
        knob->attachToParameter (audioProcessor.getAPVTS(), specs[i].id);
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    updateModeDynamicTooltips (modeSelector.getMode());
}

void AfterimageAudioProcessorEditor::updateModeDynamicTooltips (afterimage::SpectralMode mode)
{
    if (knobs.size() > (size_t) kInfluenceKnobIndex)
        knobs[(size_t) kInfluenceKnobIndex]->setTooltip (afterimage::tooltips::influenceForMode (mode));
    if (knobs.size() > (size_t) kBlurKnobIndex)
        knobs[(size_t) kBlurKnobIndex]->setTooltip (afterimage::tooltips::blurForMode (mode));
}

void AfterimageAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (AfterimageLookAndFeel::background().brighter (0.04f),
                             bounds.getCentreX(), bounds.getY(),
                             AfterimageLookAndFeel::background().darker (0.12f),
                             bounds.getCentreX(), bounds.getBottom(),
                             false);
    g.setGradientFill (bg);
    g.fillAll();

    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillRect (bounds.removeFromLeft (20.0f));
    bounds = getLocalBounds().toFloat();
    g.fillRect (bounds.removeFromRight (20.0f));

    if (! dockBounds_.isEmpty())
        AfterimageLookAndFeel::paintGlassDock (g, dockBounds_);

    for (const auto& div : dockDividers_)
        if (! div.isEmpty())
            AfterimageLookAndFeel::paintDockDivider (g, div);
}

void AfterimageAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();
    const float scale = juce::jlimit (0.85f, 1.15f, (float) W / 1000.0f);

    const int margin = juce::jmax (12, juce::roundToInt (18.0f * scale));
    const int headerH = juce::jmax (44, juce::roundToInt (50.0f * scale));
    const int dockH = juce::jmax (110, juce::roundToInt (124.0f * scale));

    auto area = getLocalBounds().reduced (margin);

    // Header: wordmark | license | fill | preset | modes | meters | bypass
    // Fill sits with title/license (spectral buffer fullness), not beside level meters.
    auto top = area.removeFromTop (headerH);
    const int titleW = juce::jlimit (140, 220, W / 5);
    titleLabel.setBounds (top.removeFromLeft (titleW).reduced (0, juce::roundToInt (6.0f * scale)));

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    if (licensePanel != nullptr)
    {
        top.removeFromLeft (8);
        const int chipW = juce::jlimit (140, 220, juce::roundToInt (180.0f * scale));
        licensePanel->setBounds (top.removeFromLeft (chipW).reduced (0, juce::roundToInt (12.0f * scale)));
    }
#endif

    top.removeFromLeft (10);
    memoryStatusLabel.setBounds (top.removeFromLeft (juce::jmax (68, juce::roundToInt (76.0f * scale)))
                                      .reduced (0, juce::roundToInt (14.0f * scale)));
    top.removeFromLeft (10);

    const int bypassW = juce::jmax (52, juce::roundToInt (58.0f * scale));
    bypassButton.setBounds (top.removeFromRight (bypassW).reduced (2, juce::roundToInt (6.0f * scale)));
    top.removeFromRight (10);
    meterDisplay.setBounds (top.removeFromRight (juce::roundToInt (48.0f * scale))
                                .reduced (0, juce::roundToInt (8.0f * scale)));
    top.removeFromRight (12);

    const int modeW = juce::jlimit (180, 280, top.getWidth() - 150);
    modeSelector.setBounds (top.removeFromRight (modeW).reduced (0, juce::roundToInt (10.0f * scale)));
    top.removeFromRight (8);
    presetBox.setBounds (top.removeFromRight (juce::jmin (top.getWidth(), juce::roundToInt (150.0f * scale)))
                             .reduced (0, juce::roundToInt (12.0f * scale)));

    // Dock
    auto dock = area.removeFromBottom (dockH);
    dockBounds_ = dock.toFloat();
    auto dockInner = dock.reduced (juce::roundToInt (14.0f * scale), juce::roundToInt (8.0f * scale));

    // MATCH + FREEZE evenly spaced between Memory Well and dock
    const int controlSize = juce::jmin (juce::roundToInt (68.0f * scale),
                                        juce::jmax (52, area.getHeight() / 5));
    const int sideGap = juce::jmax (14, juce::roundToInt (18.0f * scale));
    const int controlBandH = controlSize + sideGap * 2;
    auto controlBand = area.removeFromBottom (controlBandH);
    const int pairGap = juce::jmax (14, juce::roundToInt (18.0f * scale));
    const int pairW = controlSize * 2 + pairGap;
    auto pair = controlBand.withSizeKeepingCentre (pairW, controlSize);
    gainMatchButton.setBounds (pair.removeFromLeft (controlSize));
    pair.removeFromLeft (pairGap);
    freezeButton.setBounds (pair.removeFromLeft (controlSize));

    memoryWell.setBounds (area);

    // Grouped knobs: Memory(2) | Spectral(4) | Output(2)
    dockDividers_ = {};
    if ((int) knobs.size() == kDockCount)
    {
        const int gap = juce::roundToInt (10.0f * scale);
        const int totalUnits = 2 + 4 + 2;
        const int dividerSlots = 2;
        const int usable = dockInner.getWidth() - gap * dividerSlots;
        const float unit = (float) usable / (float) totalUnits;

        auto placeGroup = [&] (int start, int count, DockGroup /*group*/)
        {
            const int groupW = juce::roundToInt (unit * (float) count);
            auto groupArea = dockInner.removeFromLeft (groupW);
            const int cellW = groupArea.getWidth() / count;
            for (int i = 0; i < count; ++i)
            {
                auto cell = groupArea.removeFromLeft (cellW).reduced (2, 0);
                knobs[static_cast<std::size_t> (start + i)]->setBounds (cell);
            }
        };

        placeGroup (0, 2, DockGroup::Memory);
        dockDividers_[0] = dockInner.removeFromLeft (gap).toFloat();
        placeGroup (2, 4, DockGroup::Spectral);
        dockDividers_[1] = dockInner.removeFromLeft (gap).toFloat();
        placeGroup (6, 2, DockGroup::Output);
    }

    juce::ignoreUnused (H);
}

void AfterimageAudioProcessorEditor::timerCallback()
{
    meterDisplay.setLevels (audioProcessor.getInputLevel(), audioProcessor.getOutputLevel());

    audioProcessor.getEngine().getSnapshotPublisher().copyLatest (snapCache_);
    memoryWell.applySnapshot (snapCache_);

    if (auto* influence = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idInfluence))
        memoryWell.setInfluence (influence->load());
    if (auto* memory = audioProcessor.getAPVTS().getRawParameterValue (afterimage::constants::idMemoryLength))
    {
        const float sec = memory->load();
        const float norm = juce::jlimit (0.0f, 1.0f,
            (sec - afterimage::constants::memoryLengthMinSec)
                / (afterimage::constants::memoryLengthMaxSec - afterimage::constants::memoryLengthMinSec));
        memoryWell.setMemoryLengthNorm (norm);
    }

    const int activity = juce::roundToInt (snapCache_.historyFill * 100.0f);
    const juce::String memText = "FILL " + juce::String (activity) + "%";
    if (memText != cachedMemoryStatus_)
    {
        cachedMemoryStatus_ = memText;
        memoryStatusLabel.setText (memText, juce::dontSendNotification);
    }

    // Live knob / automation edits must flip the menu off the factory name.
    audioProcessor.refreshProgramStatus();
    const int wantId = audioProcessor.isCustomProgram()
                           ? 0
                           : audioProcessor.getCurrentProgram() + 1;
    if (wantId == 0)
    {
        if (presetBox.getSelectedId() != 0)
            presetBox.setSelectedId (0, juce::dontSendNotification);
    }
    else if (presetBox.getSelectedId() != wantId)
    {
        presetBox.setSelectedId (wantId, juce::dontSendNotification);
    }

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    // Periodic LicenseManager::refresh() on the message thread only (not every paint).
    // UI chip/overlay update follows manager refresh so trial expiry / activation stick.
    if (licensePanel != nullptr)
    {
        const int ticks = afterimage::constants::uiTimerHz
                          * afterimage::constants::licenseRefreshIntervalSec;
        if (++licenseRefreshCounter_ >= ticks)
        {
            licenseRefreshCounter_ = 0;
            audioProcessor.getLicenseManager().refresh();
            licensePanel->refreshStatus();
        }
    }
#endif

    const auto* specs = dockItems();
    for (size_t i = 0; i < knobs.size() && i < (size_t) kDockCount; ++i)
    {
        if (auto* param = audioProcessor.getAPVTS().getParameter (specs[i].id))
            knobs[i]->setValueText (param->getCurrentValueAsText());
    }

}

juce::AudioProcessorEditor* AfterimageAudioProcessor::createEditor()
{
#if defined (AFTERIMAGE_UNIT_TESTS)
    return nullptr;
#else
    return new AfterimageAudioProcessorEditor (*this);
#endif
}
