#include "AfterimageKnob.h"
#include "AfterimageLookAndFeel.h"

AfterimageKnob::AfterimageKnob()
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setMouseDragSensitivity (200);
    addAndMakeVisible (slider);

    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
    nameLabel.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary().withAlpha (0.85f));
    valueLabel.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (valueLabel);
}

void AfterimageKnob::setNameLabel (const juce::String& name)
{
    nameLabel.setText (name, juce::dontSendNotification);
}

void AfterimageKnob::setValueText (const juce::String& text)
{
    valueLabel.setText (text, juce::dontSendNotification);
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
