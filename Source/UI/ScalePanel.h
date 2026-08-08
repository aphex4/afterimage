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
    Scale Accentuator view: exclusive Scale Snap vs Auto-Tune, shared root/scale.
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
        style (title_, "SCALE ACCENTUATOR");
        style (snapTitle_, "SCALE SNAP");
        style (tuneTitle_, "AUTO-TUNE");
        addAndMakeVisible (title_);
        addAndMakeVisible (snapTitle_);
        addAndMakeVisible (tuneTitle_);

        pathBox_.addItem ("Off", 1);
        pathBox_.addItem ("Scale Snap", 2);
        pathBox_.addItem ("Auto-Tune", 3);
        pathBox_.setTooltip ("Exclusive pitch path: Off, Scale Snap, or Auto-Tune.");
        addAndMakeVisible (pathBox_);

        rootBox_.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        scaleBox_.addItemList ({ "Major", "Nat. Minor", "Dorian", "Pent Major", "Pent Minor", "Chromatic" }, 1);
        rootBox_.setTooltip (afterimage::tooltips::unavailable); // replaced below after attach
        addAndMakeVisible (rootBox_);
        addAndMakeVisible (scaleBox_);

        colorKnob_.setNameLabel ("COLOR");
        colorKnob_.setTooltip ("Scale Snap color / resonance. Past 100% adds in-key resonance.");
        transKnob_.setNameLabel ("TRANSIENT");
        transKnob_.setTooltip ("Preserves attacks while Scale Snap reshapes tone.");
        speedKnob_.setNameLabel ("RETUNE");
        speedKnob_.setTooltip ("Auto-Tune retune speed. Fast = robotic; slow = natural.");
        humanKnob_.setNameLabel ("HUMANIZE");
        humanKnob_.setTooltip ("Lowers Auto-Tune correction on sustained notes.");
        addAndMakeVisible (colorKnob_);
        addAndMakeVisible (transKnob_);
        addAndMakeVisible (speedKnob_);
        addAndMakeVisible (humanKnob_);

        midiHint_.setText ("MIDI notes set root / chord pitch-classes in real time.",
                           juce::dontSendNotification);
        midiHint_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        midiHint_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        addAndMakeVisible (midiHint_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        pathAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idPitchPath, pathBox_);
        rootAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleRoot, rootBox_);
        scaleAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idScaleType, scaleBox_);
        colorKnob_.attachToParameter (apvts, afterimage::constants::idScaleColor);
        transKnob_.attachToParameter (apvts, afterimage::constants::idScaleTransient);
        speedKnob_.attachToParameter (apvts, afterimage::constants::idRetuneSpeed);
        humanKnob_.attachToParameter (apvts, afterimage::constants::idHumanize);
        apvts_ = &apvts;
        pathBox_.onChange = [this] { refreshEnablement(); };
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
        setTxt (speedKnob_, afterimage::constants::idRetuneSpeed);
        setTxt (humanKnob_, afterimage::constants::idHumanize);
        refreshEnablement();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& r : panels_)
            if (! r.isEmpty())
                AfterimageLookAndFeel::paintGlassDock (g, r.toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        title_.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        auto pathRow = area.removeFromTop (28);
        pathBox_.setBounds (pathRow.removeFromLeft (160));
        pathRow.removeFromLeft (12);
        rootBox_.setBounds (pathRow.removeFromLeft (70));
        pathRow.removeFromLeft (8);
        scaleBox_.setBounds (pathRow.removeFromLeft (130));

        area.removeFromTop (12);
        const int gap = 12;
        const int half = (area.getHeight() - gap) / 2;
        auto snap = area.removeFromTop (half);
        area.removeFromTop (gap);
        auto tune = area.removeFromTop (half);
        panels_ = { snap, tune };

        layoutModule (snap.reduced (12, 10), snapTitle_, colorKnob_, transKnob_);
        layoutModule (tune.reduced (12, 10), tuneTitle_, speedKnob_, humanKnob_);
        midiHint_.setBounds (getLocalBounds().removeFromBottom (18).reduced (12, 0));
    }

private:
    void layoutModule (juce::Rectangle<int> r, juce::Label& title, AfterimageKnob& a, AfterimageKnob& b)
    {
        title.setBounds (r.removeFromTop (18));
        const int w = r.getWidth() / 2;
        a.setBounds (r.removeFromLeft (w).reduced (8));
        b.setBounds (r.reduced (8));
    }

    void refreshEnablement()
    {
        const int path = pathBox_.getSelectedItemIndex(); // 0 Off, 1 Snap, 2 Tune
        const bool snap = (path == 1);
        const bool tune = (path == 2);
        colorKnob_.setEnabled (snap);
        transKnob_.setEnabled (snap);
        speedKnob_.setEnabled (tune);
        humanKnob_.setEnabled (tune);
    }

    juce::Label title_, snapTitle_, tuneTitle_, midiHint_;
    juce::ComboBox pathBox_, rootBox_, scaleBox_;
    AfterimageKnob colorKnob_, transKnob_, speedKnob_, humanKnob_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pathAtt_, rootAtt_, scaleAtt_;
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    std::array<juce::Rectangle<int>, 2> panels_ {};
};
