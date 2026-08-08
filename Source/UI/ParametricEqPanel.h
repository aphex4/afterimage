#pragma once

#include "AfterimageFonts.h"
#include "AfterimageKnob.h"
#include "AfterimageLookAndFeel.h"
#include "../DSP/ParametricEQ.h"
#include "../Utilities/Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

/**
    Parametric EQ view: interactive spectrum + 8 band nodes / dials.
*/
class ParametricEqPanel : public juce::Component
{
public:
    ParametricEqPanel()
    {
        title_.setText ("PARAMETRIC EQ", juce::dontSendNotification);
        title_.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
        title_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
        addAndMakeVisible (title_);

        modeBox_.addItem ("Stereo", 1);
        modeBox_.addItem ("Left / Right", 2);
        modeBox_.addItem ("Mid / Side", 3);
        modeBox_.setTooltip ("EQ processing mode. LR/MS use independent filter state with linked controls.");
        addAndMakeVisible (modeBox_);

        typeBox_.addItemList ({ "Low Pass", "High Pass", "Low Shelf", "High Shelf", "Bell", "Notch" }, 1);
        addAndMakeVisible (typeBox_);

        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            auto& b = bandButtons_[static_cast<size_t> (i)];
            b.setButtonText (juce::String (i + 1));
            b.setClickingTogglesState (true);
            b.setRadioGroupId (0xE001);
            b.onClick = [this, i] { selectBand (i); };
            addAndMakeVisible (b);

            onButtons_[static_cast<size_t> (i)].setButtonText ("ON");
            onButtons_[static_cast<size_t> (i)].setClickingTogglesState (true);
            addAndMakeVisible (onButtons_[static_cast<size_t> (i)]);

            soloButtons_[static_cast<size_t> (i)].setButtonText (juce::CharPointer_UTF8 ("\xe2\x99\xaa")); // ♪ as stand-in; paint headphone-ish
            soloButtons_[static_cast<size_t> (i)].setButtonText ("S");
            soloButtons_[static_cast<size_t> (i)].setClickingTogglesState (true);
            soloButtons_[static_cast<size_t> (i)].setTooltip ("Solo / audition this band");
            addAndMakeVisible (soloButtons_[static_cast<size_t> (i)]);

            x4Buttons_[static_cast<size_t> (i)].setButtonText ("x4");
            x4Buttons_[static_cast<size_t> (i)].setClickingTogglesState (true);
            x4Buttons_[static_cast<size_t> (i)].setTooltip ("Steeper LP/HP slope (4 cascaded stages)");
            addAndMakeVisible (x4Buttons_[static_cast<size_t> (i)]);
        }

        freqKnob_.setNameLabel ("FREQ");
        gainKnob_.setNameLabel ("GAIN");
        qKnob_.setNameLabel ("Q");
        addAndMakeVisible (freqKnob_);
        addAndMakeVisible (gainKnob_);
        addAndMakeVisible (qKnob_);
        addAndMakeVisible (typeBox_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, afterimage::ParametricEQ& eq)
    {
        apvts_ = &apvts;
        eq_ = &eq;
        modeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, afterimage::constants::idEqChannelMode, modeBox_);

        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            const auto n = juce::String (i + 1);
            onAtt_[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, "eq" + n + "On", onButtons_[static_cast<size_t> (i)]);
            soloAtt_[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, "eq" + n + "Solo", soloButtons_[static_cast<size_t> (i)]);
            x4Att_[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, "eq" + n + "X4", x4Buttons_[static_cast<size_t> (i)]);
        }
        selectBand (0);
    }

    void setSpectrumBins (const float* bins, int num)
    {
        const int n = juce::jmin (num, afterimage::SpectrumProbe::kBins);
        for (int i = 0; i < n; ++i)
            bins_[static_cast<size_t> (i)] = bins[i];
        repaint (plotBounds_);
    }

    void refreshValueText()
    {
        if (apvts_ == nullptr) return;
        const auto n = juce::String (selected_ + 1);
        if (auto* p = apvts_->getParameter ("eq" + n + "Freq"))
            freqKnob_.setValueText (p->getCurrentValueAsText());
        if (auto* p = apvts_->getParameter ("eq" + n + "Gain"))
            gainKnob_.setValueText (p->getCurrentValueAsText());
        if (auto* p = apvts_->getParameter ("eq" + n + "Q"))
            qKnob_.setValueText (p->getCurrentValueAsText());
    }

    void paint (juce::Graphics& g) override
    {
        AfterimageLookAndFeel::paintGlassDock (g, getLocalBounds().toFloat().reduced (2.0f));

        auto r = plotBounds_.toFloat();
        if (r.isEmpty()) return;

        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (r, 6.0f);

        // Spectrum
        juce::Path spec;
        const int nb = afterimage::SpectrumProbe::kBins;
        for (int i = 0; i < nb; ++i)
        {
            const float x = r.getX() + r.getWidth() * ((float) i / (float) (nb - 1));
            const float y = r.getBottom() - r.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, bins_[static_cast<size_t> (i)]);
            if (i == 0) spec.startNewSubPath (x, y); else spec.lineTo (x, y);
        }
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.35f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));

        // EQ curve
        if (eq_ != nullptr)
        {
            juce::Path curve;
            constexpr int pts = 128;
            for (int i = 0; i < pts; ++i)
            {
                const float t = (float) i / (float) (pts - 1);
                const float hz = 20.0f * std::pow (1000.0f, t); // 20..20k log
                const float db = juce::jlimit (-24.0f, 24.0f, eq_->responseDbAt (hz));
                const float x = r.getX() + t * r.getWidth();
                const float y = r.getCentreY() - (db / 24.0f) * (r.getHeight() * 0.42f);
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (AfterimageLookAndFeel::accentCyan());
            g.strokePath (curve, juce::PathStrokeType (1.6f));

            // Nodes
            for (int b = 0; b < afterimage::constants::parametricEqBands; ++b)
            {
                const auto& band = eq_->getBand (b);
                const float t = juce::jlimit (0.0f, 1.0f,
                    (std::log (band.freqHz) - std::log (20.0f)) / (std::log (20000.0f) - std::log (20.0f)));
                const float x = r.getX() + t * r.getWidth();
                const float y = r.getCentreY() - (band.gainDb / 24.0f) * (r.getHeight() * 0.42f);
                g.setColour (b == selected_ ? AfterimageLookAndFeel::accentCyan()
                                            : AfterimageLookAndFeel::accentViolet().withAlpha (band.enabled ? 0.9f : 0.35f));
                g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            }
        }

        g.setColour (AfterimageLookAndFeel::panelEdge());
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto header = area.removeFromTop (26);
        title_.setBounds (header.removeFromLeft (140));
        modeBox_.setBounds (header.removeFromLeft (140).reduced (4, 0));

        auto bandRow = area.removeFromTop (26);
        const int cell = bandRow.getWidth() / afterimage::constants::parametricEqBands;
        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            auto c = bandRow.removeFromLeft (cell).reduced (2, 0);
            bandButtons_[static_cast<size_t> (i)].setBounds (c.removeFromLeft (c.getWidth() / 3));
            onButtons_[static_cast<size_t> (i)].setBounds (c.removeFromLeft (c.getWidth() / 2));
            soloButtons_[static_cast<size_t> (i)].setBounds (c);
        }

        auto dials = area.removeFromBottom (100);
        typeBox_.setBounds (dials.removeFromLeft (120).reduced (4, 20));
        x4Buttons_[static_cast<size_t> (selected_)].setBounds (dials.removeFromLeft (44).reduced (4, 28));
        const int kw = dials.getWidth() / 3;
        freqKnob_.setBounds (dials.removeFromLeft (kw));
        gainKnob_.setBounds (dials.removeFromLeft (kw));
        qKnob_.setBounds (dials);

        plotBounds_ = area.reduced (4, 8);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plotBounds_.contains (e.getPosition()) || eq_ == nullptr || apvts_ == nullptr)
            return;
        const int hit = hitTestNode (e.position);
        if (hit >= 0)
        {
            selectBand (hit);
            dragging_ = hit;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging_ < 0 || apvts_ == nullptr) return;
        auto r = plotBounds_.toFloat();
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float hz = std::exp (std::log (20.0f) + t * (std::log (20000.0f) - std::log (20.0f)));
        const float db = juce::jlimit (-24.0f, 24.0f,
            (r.getCentreY() - e.position.y) / (r.getHeight() * 0.42f) * 24.0f);
        setFloat ("eq" + juce::String (dragging_ + 1) + "Freq", hz);
        setFloat ("eq" + juce::String (dragging_ + 1) + "Gain", db);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging_ = -1; }

private:
    void selectBand (int index)
    {
        selected_ = juce::jlimit (0, afterimage::constants::parametricEqBands - 1, index);
        bandButtons_[static_cast<size_t> (selected_)].setToggleState (true, juce::dontSendNotification);
        if (apvts_ == nullptr) return;

        freqAtt_.reset();
        gainAtt_.reset();
        qAtt_.reset();
        typeAtt_.reset();
        const auto n = juce::String (selected_ + 1);
        freqKnob_.attachToParameter (*apvts_, "eq" + n + "Freq");
        gainKnob_.attachToParameter (*apvts_, "eq" + n + "Gain");
        qKnob_.attachToParameter (*apvts_, "eq" + n + "Q");
        typeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            *apvts_, "eq" + n + "Type", typeBox_);
        resized();
        refreshValueText();
        repaint();
    }

    int hitTestNode (juce::Point<float> p) const
    {
        if (eq_ == nullptr) return -1;
        auto r = plotBounds_.toFloat();
        for (int b = 0; b < afterimage::constants::parametricEqBands; ++b)
        {
            const auto& band = eq_->getBand (b);
            const float t = juce::jlimit (0.0f, 1.0f,
                (std::log (band.freqHz) - std::log (20.0f)) / (std::log (20000.0f) - std::log (20.0f)));
            const float x = r.getX() + t * r.getWidth();
            const float y = r.getCentreY() - (band.gainDb / 24.0f) * (r.getHeight() * 0.42f);
            if (p.getDistanceFrom ({ x, y }) < 12.0f)
                return b;
        }
        return -1;
    }

    void setFloat (const juce::String& id, float actual)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts_->getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (actual));
    }

    juce::Label title_;
    juce::ComboBox modeBox_, typeBox_;
    std::array<juce::TextButton, afterimage::constants::parametricEqBands> bandButtons_ {};
    std::array<juce::ToggleButton, afterimage::constants::parametricEqBands> onButtons_ {};
    std::array<juce::ToggleButton, afterimage::constants::parametricEqBands> soloButtons_ {};
    std::array<juce::ToggleButton, afterimage::constants::parametricEqBands> x4Buttons_ {};
    AfterimageKnob freqKnob_, gainKnob_, qKnob_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt_, typeAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt_, gainAtt_, qAtt_;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, afterimage::constants::parametricEqBands> onAtt_ {}, soloAtt_ {}, x4Att_ {};

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    afterimage::ParametricEQ* eq_ = nullptr;
    juce::Rectangle<int> plotBounds_;
    std::array<float, afterimage::SpectrumProbe::kBins> bins_ {};
    int selected_ = 0;
    int dragging_ = -1;
};
