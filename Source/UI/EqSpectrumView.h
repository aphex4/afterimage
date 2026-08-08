#pragma once

#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageKnob.h"
#include "../DSP/SpectrumProbe.h"
#include "../Utilities/Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

/**
    Compact Pre/Post EQ strip: live spectrum + 4 gain knobs (+ freq via host/automation).
*/
class EqSpectrumView : public juce::Component
{
public:
    EqSpectrumView()
    {
        title_.setJustificationType (juce::Justification::centredLeft);
        title_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        title_.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
        title_.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title_);

        for (int i = 0; i < afterimage::constants::eqBandsPerStage; ++i)
        {
            knobs_[static_cast<size_t> (i)] = std::make_unique<AfterimageKnob>();
            knobs_[static_cast<size_t> (i)]->setNameLabel ("B" + juce::String (i + 1));
            addAndMakeVisible (*knobs_[static_cast<size_t> (i)]);
        }
    }

    void setTitle (const juce::String& t) { title_.setText (t, juce::dontSendNotification); }

    void attach (juce::AudioProcessorValueTreeState& apvts,
                 const char* const* freqIds,
                 const char* const* gainIds)
    {
        for (int i = 0; i < afterimage::constants::eqBandsPerStage; ++i)
        {
            knobs_[static_cast<size_t> (i)]->attachToParameter (apvts, gainIds[i]);
            if (auto* p = apvts.getParameter (freqIds[i]))
            {
                const float hz = dynamic_cast<juce::RangedAudioParameter*> (p)->convertFrom0to1 (p->getValue());
                knobs_[static_cast<size_t> (i)]->setNameLabel (formatHz (hz));
            }
        }
        apvts_ = &apvts;
        freqIds_ = freqIds;
        gainIds_ = gainIds;
    }

    void setSpectrumBins (const float* bins, int numBins)
    {
        const int n = juce::jmin (numBins, afterimage::SpectrumProbe::kBins);
        for (int i = 0; i < n; ++i)
            bins_[static_cast<size_t> (i)] = bins[i];
        repaint();
    }

    void refreshValueText()
    {
        if (apvts_ == nullptr || gainIds_ == nullptr)
            return;
        for (int i = 0; i < afterimage::constants::eqBandsPerStage; ++i)
        {
            if (auto* p = apvts_->getParameter (gainIds_[i]))
                knobs_[static_cast<size_t> (i)]->setValueText (p->getCurrentValueAsText());
            if (freqIds_ != nullptr)
                if (auto* fp = apvts_->getParameter (freqIds_[i]))
                {
                    const float hz = dynamic_cast<juce::RangedAudioParameter*> (fp)->convertFrom0to1 (fp->getValue());
                    knobs_[static_cast<size_t> (i)]->setNameLabel (formatHz (hz));
                }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = spectrumBounds_.toFloat();
        if (r.isEmpty())
            return;

        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (r, 4.0f);

        juce::Path path;
        const int n = afterimage::SpectrumProbe::kBins;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * ((float) i / (float) (n - 1));
            const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, bins_[static_cast<size_t> (i)]);
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (1.2f));

        // EQ gain ticks
        if (apvts_ != nullptr && gainIds_ != nullptr && freqIds_ != nullptr)
        {
            for (int i = 0; i < afterimage::constants::eqBandsPerStage; ++i)
            {
                auto* fp = dynamic_cast<juce::RangedAudioParameter*> (apvts_->getParameter (freqIds_[i]));
                auto* gp = dynamic_cast<juce::RangedAudioParameter*> (apvts_->getParameter (gainIds_[i]));
                if (fp == nullptr || gp == nullptr)
                    continue;
                const float hz = fp->convertFrom0to1 (fp->getValue());
                const float gdb = gp->convertFrom0to1 (gp->getValue());
                const float t = juce::jlimit (0.0f, 1.0f,
                    (std::log (juce::jlimit (40.0f, 16000.0f, hz)) - std::log (40.0f))
                        / (std::log (16000.0f) - std::log (40.0f)));
                const float x = r.getX() + t * r.getWidth();
                const float y = r.getCentreY() - (gdb / 18.0f) * (r.getHeight() * 0.42f);
                g.setColour (AfterimageLookAndFeel::accentViolet().withAlpha (0.85f));
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }

        g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title_.setBounds (area.removeFromTop (14));
        auto knobRow = area.removeFromBottom (58);
        spectrumBounds_ = area.reduced (2, 2);

        const int cell = knobRow.getWidth() / afterimage::constants::eqBandsPerStage;
        for (int i = 0; i < afterimage::constants::eqBandsPerStage; ++i)
            knobs_[static_cast<size_t> (i)]->setBounds (knobRow.removeFromLeft (cell).reduced (2, 0));
    }

private:
    static juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 1) + "k";
        return juce::String (juce::roundToInt (hz));
    }

    juce::Label title_;
    std::array<std::unique_ptr<AfterimageKnob>, afterimage::constants::eqBandsPerStage> knobs_;
    std::array<float, afterimage::SpectrumProbe::kBins> bins_ {};
    juce::Rectangle<int> spectrumBounds_;
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    const char* const* freqIds_ = nullptr;
    const char* const* gainIds_ = nullptr;
};
