#include "AfterimageKnob.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"

AfterimageKnob::AfterimageKnob()
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setMouseDragSensitivity (200);
    addAndMakeVisible (slider);

    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    nameLabel.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary().withAlpha (0.85f));
    valueLabel.setFont (AfterimageFonts::get (AfterimageFontRole::ParameterValue));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);
}

void AfterimageKnob::setNameLabel (const juce::String& name)
{
    nameLabel.setText (name, juce::dontSendNotification);
}

void AfterimageKnob::setValueText (const juce::String& text)
{
    if (text == cachedValueText_)
        return;
    cachedValueText_ = text;
    valueLabel.setText (text, juce::dontSendNotification);
}

void AfterimageKnob::setTooltip (const juce::String& tip)
{
    enabledTooltip_ = tip;
    applyActiveTooltip();
}

void AfterimageKnob::enablementChanged()
{
    applyActiveTooltip();
}

void AfterimageKnob::applyActiveTooltip()
{
    const juce::String tip = isEnabled() ? enabledTooltip_
                                         : juce::String (afterimage::tooltips::unavailable);
    SettableTooltipClient::setTooltip (tip);
    slider.setTooltip (tip);
}

void AfterimageKnob::attachToParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramId, slider);
}

void AfterimageKnob::resized()
{
    auto area = getLocalBounds();
    nameLabel.setBounds (area.removeFromTop (14));
    valueLabel.setBounds (area.removeFromBottom (14));
    slider.setBounds (area.reduced (2));
}
