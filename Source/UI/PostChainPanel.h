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

/** FX page: simplified Reverb, Formant, De-esser. */
class PostChainPanel : public juce::Component
{
public:
    PostChainPanel()
    {
        auto styleHeader = [] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
            l.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary().withAlpha (0.9f));
            l.setJustificationType (juce::Justification::centredLeft);
            l.setInterceptsMouseClicks (false, false);
        };

        styleHeader (pageTitle_, "FX");
        styleHeader (reverbTitle_, "REVERB");
        styleHeader (formantTitle_, "FORMANT");
        styleHeader (deEssTitle_, "DE-ESSER");
        addAndMakeVisible (pageTitle_);
        addAndMakeVisible (reverbTitle_);
        addAndMakeVisible (formantTitle_);
        addAndMakeVisible (deEssTitle_);

        reverbOn_.setButtonText ("ON");
        reverbOn_.setTooltip (afterimage::tooltips::reverbEnable);
        formantOn_.setButtonText ("ON");
        formantOn_.setTooltip (afterimage::tooltips::formantEnable);
        deEssOn_.setButtonText ("ON");
        deEssOn_.setTooltip (afterimage::tooltips::deEsserEnable);
        safeBass_.setButtonText ("SAFE BASS");
        safeBass_.setTooltip (afterimage::tooltips::reverbSafeBass);
        addAndMakeVisible (reverbOn_);
        addAndMakeVisible (formantOn_);
        addAndMakeVisible (deEssOn_);
        addAndMakeVisible (safeBass_);

        reverbTypeBox_.addItem ("Spring", 1);
        reverbTypeBox_.addItem ("Hall", 2);
        reverbTypeBox_.addItem ("Room", 3);
        reverbTypeBox_.setTooltip (afterimage::tooltips::reverbType);
        addAndMakeVisible (reverbTypeBox_);

        wetKnob_.setNameLabel ("MIX");
        wetKnob_.setTooltip (afterimage::tooltips::reverbWet);
        addAndMakeVisible (wetKnob_);

        formantSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
        formantSlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        formantSlider_.setTooltip (afterimage::tooltips::formant);
        formantSlider_.setColour (juce::Slider::trackColourId, AfterimageLookAndFeel::accentCyan().withAlpha (0.35f));
        formantSlider_.setColour (juce::Slider::thumbColourId, AfterimageLookAndFeel::accentCyan());
        formantSlider_.setColour (juce::Slider::backgroundColourId, AfterimageLookAndFeel::meterTrack());
        addAndMakeVisible (formantSlider_);
        formantLow_.setText ("LOW", juce::dontSendNotification);
        formantHigh_.setText ("HIGH", juce::dontSendNotification);
        for (auto* l : { &formantLow_, &formantHigh_ })
        {
            l->setFont (AfterimageFonts::get (AfterimageFontRole::Status));
            l->setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*l);
        }
        formantLow_.setJustificationType (juce::Justification::centredLeft);
        formantHigh_.setJustificationType (juce::Justification::centredRight);

        deEssKnob_.setNameLabel ("INTENSITY");
        deEssKnob_.setTooltip (afterimage::tooltips::deEsser);
        addAndMakeVisible (deEssKnob_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        reverbOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idReverbEnabled, reverbOn_);
        formantOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idFormantEnabled, formantOn_);
        deEssOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idDeEsserEnabled, deEssOn_);
        safeBassAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idReverbSafeBass, safeBass_);
        typeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idReverbType, reverbTypeBox_);
        wetKnob_.attachToParameter (apvts, afterimage::constants::idReverbWet);
        formantAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, afterimage::constants::idFormant, formantSlider_);
        deEssKnob_.attachToParameter (apvts, afterimage::constants::idDeEsser);
        apvts_ = &apvts;
    }

    void refreshValueText()
    {
        if (apvts_ == nullptr)
            return;
        if (auto* p = apvts_->getParameter (afterimage::constants::idReverbWet))
            wetKnob_.setValueText (p->getCurrentValueAsText());
        if (auto* p = apvts_->getParameter (afterimage::constants::idDeEsser))
            deEssKnob_.setValueText (p->getCurrentValueAsText());
        if (auto* p = apvts_->getParameter (afterimage::constants::idFormant))
            formantValue_ = p->getCurrentValueAsText();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& panel : panelBounds_)
        {
            if (panel.isEmpty())
                continue;
            AfterimageLookAndFeel::paintGlassDock (g, panel.toFloat());
        }

        if (! formantTrack_.isEmpty())
        {
            g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.25f));
            g.fillRoundedRectangle (formantTrack_.toFloat(), 3.0f);
            if (formantValue_.isNotEmpty())
            {
                g.setFont (AfterimageFonts::get (AfterimageFontRole::ParameterValue));
                g.setColour (AfterimageLookAndFeel::textPrimary().withAlpha (0.8f));
                g.drawText (formantValue_, formantTrack_.translated (0, -18), juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        pageTitle_.setBounds (area.removeFromTop (22));
        area.removeFromTop (6);

        const int gap = juce::jmax (10, area.getHeight() / 28);
        const int totalGaps = gap * 2;
        const int usable = juce::jmax (120, area.getHeight() - totalGaps);
        const int reverbH = juce::roundToInt ((float) usable * 0.42f);
        const int formantH = juce::roundToInt ((float) usable * 0.28f);
        const int deEssH = usable - reverbH - formantH;

        auto reverbArea = area.removeFromTop (reverbH);
        area.removeFromTop (gap);
        auto formantArea = area.removeFromTop (formantH);
        area.removeFromTop (gap);
        auto deEssArea = area.removeFromTop (deEssH);

        panelBounds_ = { reverbArea, formantArea, deEssArea };

        layoutReverb (reverbArea.reduced (10, 8));
        layoutFormant (formantArea.reduced (10, 8));
        layoutDeEss (deEssArea.reduced (10, 8));
    }

private:
    void layoutReverb (juce::Rectangle<int> r)
    {
        auto header = r.removeFromTop (28);
        reverbTitle_.setBounds (header.removeFromLeft (80));
        reverbOn_.setBounds (header.removeFromLeft (52).reduced (2, 2));
        safeBass_.setBounds (header.removeFromLeft (110).reduced (4, 2));
        wetKnob_.setBounds (header.removeFromRight (70).withHeight (juce::jmin (header.getHeight() + 48, 84)).translated (0, -6));
        reverbTypeBox_.setBounds (header.removeFromLeft (juce::jmin (140, header.getWidth() - 8)).reduced (4, 2));
    }

    void layoutFormant (juce::Rectangle<int> r)
    {
        auto header = r.removeFromTop (22);
        formantTitle_.setBounds (header.removeFromLeft (90));
        formantOn_.setBounds (header.removeFromLeft (52).reduced (2, 0));
        auto labels = r.removeFromBottom (16);
        formantLow_.setBounds (labels.removeFromLeft (40));
        formantHigh_.setBounds (labels.removeFromRight (40));
        formantTrack_ = r.reduced (4, 6);
        formantSlider_.setBounds (formantTrack_);
    }

    void layoutDeEss (juce::Rectangle<int> r)
    {
        auto header = r.removeFromTop (22);
        deEssTitle_.setBounds (header.removeFromLeft (90));
        deEssOn_.setBounds (header.removeFromLeft (52).reduced (2, 0));
        deEssKnob_.setBounds (r.withSizeKeepingCentre (juce::jmin (90, r.getWidth()), juce::jmin (90, r.getHeight())));
    }

    juce::Label pageTitle_, reverbTitle_, formantTitle_, deEssTitle_;
    juce::Label formantLow_, formantHigh_;
    ChipToggle reverbOn_, formantOn_, deEssOn_, safeBass_;
    juce::ComboBox reverbTypeBox_;
    AfterimageKnob wetKnob_;
    AfterimageKnob deEssKnob_;
    juce::Slider formantSlider_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverbOnAtt_, formantOnAtt_, deEssOnAtt_, safeBassAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> formantAttachment_;
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;

    std::array<juce::Rectangle<int>, 3> panelBounds_ {};
    juce::Rectangle<int> formantTrack_;
    juce::String formantValue_;
};
