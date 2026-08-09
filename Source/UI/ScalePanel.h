#pragma once

#include "AfterimageFonts.h"
#include "AfterimageKnob.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"
#include "../Utilities/Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

/**
    HARMONICS page: scale-aware spectral sweetener (not Autotune).
*/
class ScalePanel : public juce::Component
{
public:
    ScalePanel()
    {
        auto style = [] (juce::Label& l, const char* t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
            l.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
            l.setJustificationType (juce::Justification::centredLeft);
        };
        style (title_, "HARMONICS");
        style (subtitle_, "Scale-aware spectral sweetener — not pitch correction.");
        addAndMakeVisible (title_);
        addAndMakeVisible (subtitle_);

        enableBtn_.setButtonText ("ON");
        enableBtn_.setClickingTogglesState (true);
        enableBtn_.setTooltip ("Enable HARMONICS sweetener. Off is exact identity.");
        addAndMakeVisible (enableBtn_);

        rootBox_.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        scaleBox_.addItemList ({ "Major", "Nat. Minor", "Dorian", "Pent Major", "Pent Minor", "Chromatic" }, 1);
        rootBox_.setTooltip ("Scale root / tonic pitch class.");
        scaleBox_.setTooltip ("Scale type that defines in-key pitch classes.");
        addAndMakeVisible (rootBox_);
        addAndMakeVisible (scaleBox_);

        colorKnob_.setNameLabel ("COLOR");
        colorKnob_.setTooltip (afterimage::tooltips::harmonicsColor);
        transKnob_.setNameLabel ("TRANSIENT");
        transKnob_.setTooltip (afterimage::tooltips::harmonicsTransient);
        addAndMakeVisible (colorKnob_);
        addAndMakeVisible (transKnob_);

        midiHint_.setText ("MIDI notes set root / chord pitch-classes in real time.",
                           juce::dontSendNotification);
        midiHint_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        midiHint_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        addAndMakeVisible (midiHint_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        enableAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idHarmonicsEnabled, enableBtn_);
        rootAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleRoot, rootBox_);
        scaleAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleType, scaleBox_);
        colorKnob_.attachToParameter (apvts, afterimage::constants::idScaleColor);
        transKnob_.attachToParameter (apvts, afterimage::constants::idScaleTransient);
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
        setTxt (colorKnob_, afterimage::constants::idScaleColor);
        setTxt (transKnob_, afterimage::constants::idScaleTransient);
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
        const int w = knobs.getWidth() / 2;
        colorKnob_.setBounds (knobs.removeFromLeft (w).reduced (12));
        transKnob_.setBounds (knobs.reduced (12));
        midiHint_.setBounds (getLocalBounds().removeFromBottom (22).reduced (16, 0));
    }

private:
    void refreshEnablement()
    {
        const bool on = enableBtn_.getToggleState();
        rootBox_.setEnabled (on);
        scaleBox_.setEnabled (on);
        colorKnob_.setEnabled (on);
        transKnob_.setEnabled (on);
    }

    juce::Label title_, subtitle_, midiHint_;
    juce::ToggleButton enableBtn_;
    juce::ComboBox rootBox_, scaleBox_;
    AfterimageKnob colorKnob_, transKnob_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt_, scaleAtt_;
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::Rectangle<int> panel_;
};
