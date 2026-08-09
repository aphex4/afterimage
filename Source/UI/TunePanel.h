#pragma once

#include "AfterimageFonts.h"
#include "AfterimageKnob.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"
#include "ChipToggle.h"
#include "../Utilities/Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

/** TUNE page: automatic monophonic pitch correction. */
class TunePanel : public juce::Component
{
public:
    TunePanel()
    {
        title_.setText ("TUNE", juce::dontSendNotification);
        title_.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
        title_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
        subtitle_.setText ("Automatic pitch correction to Root + Scale.", juce::dontSendNotification);
        subtitle_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        subtitle_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        addAndMakeVisible (title_);
        addAndMakeVisible (subtitle_);

        enableBtn_.setButtonText ("ON");
        enableBtn_.setTooltip (afterimage::tooltips::tuneEnable);
        addAndMakeVisible (enableBtn_);

        rootBox_.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        scaleBox_.addItemList ({ "Major", "Nat. Minor", "Dorian", "Pent Major", "Pent Minor", "Chromatic" }, 1);
        rootBox_.setTooltip (afterimage::tooltips::tuneRoot);
        scaleBox_.setTooltip (afterimage::tooltips::tuneScale);
        addAndMakeVisible (rootBox_);
        addAndMakeVisible (scaleBox_);

        retuneKnob_.setNameLabel ("RETUNE");
        retuneKnob_.setTooltip (afterimage::tooltips::tuneRetune);
        humanizeKnob_.setNameLabel ("HUMANIZE");
        humanizeKnob_.setTooltip (afterimage::tooltips::tuneHumanize);
        amountKnob_.setNameLabel ("AMOUNT");
        amountKnob_.setTooltip (afterimage::tooltips::tuneAmount);
        addAndMakeVisible (retuneKnob_);
        addAndMakeVisible (humanizeKnob_);
        addAndMakeVisible (amountKnob_);

        midiHint_.setText ("Held MIDI notes override the scale pitch-classes.", juce::dontSendNotification);
        midiHint_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        midiHint_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        addAndMakeVisible (midiHint_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        enableAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idTuneEnabled, enableBtn_);
        rootAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleRoot, rootBox_);
        scaleAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleType, scaleBox_);
        retuneKnob_.attachToParameter (apvts, afterimage::constants::idRetune);
        humanizeKnob_.attachToParameter (apvts, afterimage::constants::idHumanize);
        amountKnob_.attachToParameter (apvts, afterimage::constants::idTuneAmount);
        apvts_ = &apvts;
        enableBtn_.onClick = [this] { refreshEnablement(); };
        refreshEnablement();
    }

    void refreshValueText()
    {
        if (apvts_ == nullptr) return;
        auto setTxt = [&] (AfterimageKnob& k, const char* id)
        {
            if (auto* p = apvts_->getParameter (id))
                k.setValueText (p->getCurrentValueAsText());
        };
        setTxt (retuneKnob_, afterimage::constants::idRetune);
        setTxt (humanizeKnob_, afterimage::constants::idHumanize);
        setTxt (amountKnob_, afterimage::constants::idTuneAmount);
        refreshEnablement();
    }

    void paint (juce::Graphics& g) override
    {
        if (! panel_.isEmpty())
            AfterimageLookAndFeel::paintGlassDock (g, panel_.toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title_.setBounds (area.removeFromTop (28));
        subtitle_.setBounds (area.removeFromTop (20));
        area.removeFromTop (12);

        auto pathRow = area.removeFromTop (36);
        enableBtn_.setBounds (pathRow.removeFromLeft (72).reduced (0, 4));
        pathRow.removeFromLeft (16);
        rootBox_.setBounds (pathRow.removeFromLeft (80).reduced (0, 4));
        pathRow.removeFromLeft (10);
        scaleBox_.setBounds (pathRow.removeFromLeft (150).reduced (0, 4));

        area.removeFromTop (20);
        panel_ = area.removeFromTop (juce::jmin (220, area.getHeight() - 24));
        auto knobs = panel_.reduced (24, 28);
        const int w = knobs.getWidth() / 3;
        retuneKnob_.setBounds (knobs.removeFromLeft (w).reduced (10));
        humanizeKnob_.setBounds (knobs.removeFromLeft (w).reduced (10));
        amountKnob_.setBounds (knobs.reduced (10));
        midiHint_.setBounds (getLocalBounds().removeFromBottom (22).reduced (16, 0));
    }

private:
    void refreshEnablement()
    {
        const bool on = enableBtn_.getToggleState();
        rootBox_.setEnabled (on);
        scaleBox_.setEnabled (on);
        retuneKnob_.setEnabled (on);
        humanizeKnob_.setEnabled (on);
        amountKnob_.setEnabled (on);
    }

    juce::Label title_, subtitle_, midiHint_;
    ChipToggle enableBtn_;
    juce::ComboBox rootBox_, scaleBox_;
    AfterimageKnob retuneKnob_, humanizeKnob_, amountKnob_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt_, scaleAtt_;
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::Rectangle<int> panel_;
};
