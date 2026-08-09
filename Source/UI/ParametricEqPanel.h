#pragma once

#include "AfterimageFonts.h"
#include "AfterimageKnob.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"
#include "ChipToggle.h"
#include "../DSP/ParametricEQ.h"
#include "../DSP/SpectrumProbe.h"
#include "../Utilities/Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

/**
    Parametric EQ: pre-EQ spectrum behind response curve.
    Band panel: TYPE FREQ GAIN Q SLOPE ON SOLO.
    SLOPE (12 dB | 48 dB) for LP/HP only; hidden for Bell/Shelf/Notch.
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

        masterOn_.setButtonText ("ON");
        masterOn_.setTooltip (afterimage::tooltips::eqEnable);
        addAndMakeVisible (masterOn_);

        typeLabel_.setText ("TYPE", juce::dontSendNotification);
        freqLabel_.setText ("FREQ", juce::dontSendNotification);
        gainLabel_.setText ("GAIN", juce::dontSendNotification);
        qLabel_.setText ("Q", juce::dontSendNotification);
        slopeLabel_.setText ("SLOPE", juce::dontSendNotification);
        for (auto* l : { &typeLabel_, &freqLabel_, &gainLabel_, &qLabel_, &slopeLabel_ })
        {
            l->setFont (AfterimageFonts::get (AfterimageFontRole::Status));
            l->setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
            l->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*l);
        }

        typeBox_.addItemList ({ "Low Pass", "High Pass", "Low Shelf", "High Shelf", "Bell", "Notch" }, 1);
        typeBox_.setTooltip (afterimage::tooltips::eqType);
        typeBox_.onChange = [this] { refreshSlopeVisibility(); };
        addAndMakeVisible (typeBox_);

        slopeBox_.addItem ("12 dB", 1);
        slopeBox_.addItem ("48 dB", 2);
        slopeBox_.setTooltip (afterimage::tooltips::eqSlope);
        slopeBox_.onChange = [this]
        {
            if (apvts_ == nullptr || updatingSlope_) return;
            const auto n = juce::String (selected_ + 1);
            if (auto* p = apvts_->getParameter ("eq" + n + "X4"))
                p->setValueNotifyingHost (slopeBox_.getSelectedId() == 2 ? 1.0f : 0.0f);
        };
        addAndMakeVisible (slopeBox_);

        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            auto& b = bandButtons_[static_cast<size_t> (i)];
            b.setButtonText (juce::String (i + 1));
            b.setClickingTogglesState (true);
            b.setRadioGroupId (0xE001);
            b.onClick = [this, i] { selectBand (i); };
            addAndMakeVisible (b);

            onButtons_[static_cast<size_t> (i)].setButtonText ("ON");
            addAndMakeVisible (onButtons_[static_cast<size_t> (i)]);

            soloButtons_[static_cast<size_t> (i)].setButtonText ("SOLO");
            soloButtons_[static_cast<size_t> (i)].setTooltip (afterimage::tooltips::eqSolo);
            soloButtons_[static_cast<size_t> (i)].onClick = [this, i]
            {
                exclusiveSolo (i, soloButtons_[static_cast<size_t> (i)].getToggleState());
            };
            addAndMakeVisible (soloButtons_[static_cast<size_t> (i)]);
        }

        freqKnob_.setNameLabel ("FREQ");
        gainKnob_.setNameLabel ("GAIN");
        qKnob_.setNameLabel ("Q");
        addAndMakeVisible (freqKnob_);
        addAndMakeVisible (gainKnob_);
        addAndMakeVisible (qKnob_);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, afterimage::ParametricEQ& eq)
    {
        apvts_ = &apvts;
        eq_ = &eq;
        masterAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, afterimage::constants::idEqEnabled, masterOn_);

        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            const auto n = juce::String (i + 1);
            onAtt_[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, "eq" + n + "On", onButtons_[static_cast<size_t> (i)]);
            soloAtt_[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, "eq" + n + "Solo", soloButtons_[static_cast<size_t> (i)]);
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
        syncSlopeFromParam();
        refreshSlopeVisibility();
    }

    void paint (juce::Graphics& g) override
    {
        AfterimageLookAndFeel::paintGlassDock (g, getLocalBounds().toFloat().reduced (2.0f));

        auto r = plotBounds_.toFloat();
        if (r.isEmpty()) return;

        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (r, 6.0f);

        // Subtle grid
        g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.35f));
        for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
        {
            const float y = r.getCentreY() - (db / 24.0f) * (r.getHeight() * 0.42f);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float t = juce::jlimit (0.0f, 1.0f,
                (std::log (hz) - std::log (20.0f)) / (std::log (20000.0f) - std::log (20.0f)));
            const float x = r.getX() + t * r.getWidth();
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }

        // Translucent pre-EQ spectrum fill
        juce::Path spec;
        const int nb = afterimage::SpectrumProbe::kBins;
        for (int i = 0; i < nb; ++i)
        {
            const float x = r.getX() + r.getWidth() * ((float) i / (float) juce::jmax (1, nb - 1));
            const float y = r.getBottom() - r.getHeight() * 0.55f * juce::jlimit (0.0f, 1.0f, bins_[static_cast<size_t> (i)]);
            if (i == 0) spec.startNewSubPath (x, r.getBottom());
            spec.lineTo (x, y);
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.16f));
        g.fillPath (spec);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.40f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));

        if (eq_ != nullptr)
        {
            juce::Path curve;
            constexpr int pts = 160;
            for (int i = 0; i < pts; ++i)
            {
                const float t = (float) i / (float) (pts - 1);
                const float hz = 20.0f * std::pow (1000.0f, t);
                const float db = juce::jlimit (-24.0f, 24.0f, eq_->responseDbAt (hz));
                const float x = r.getX() + t * r.getWidth();
                const float y = r.getCentreY() - (db / 24.0f) * (r.getHeight() * 0.42f);
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (AfterimageLookAndFeel::accentCyan().brighter (0.15f));
            g.strokePath (curve, juce::PathStrokeType (2.0f));

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
        masterOn_.setBounds (header.removeFromLeft (56).reduced (4, 0));

        auto bandRow = area.removeFromTop (28);
        const int cell = bandRow.getWidth() / afterimage::constants::parametricEqBands;
        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            auto c = bandRow.removeFromLeft (cell).reduced (2, 0);
            bandButtons_[static_cast<size_t> (i)].setBounds (c.removeFromLeft (22));
            onButtons_[static_cast<size_t> (i)].setBounds (c.removeFromLeft (c.getWidth() / 2).reduced (1, 2));
            soloButtons_[static_cast<size_t> (i)].setBounds (c.reduced (1, 2));
        }

        auto dials = area.removeFromBottom (118);
        auto labels = dials.removeFromTop (16);
        const int labelW = labels.getWidth() / 5;
        typeLabel_.setBounds (labels.removeFromLeft (labelW));
        freqLabel_.setBounds (labels.removeFromLeft (labelW));
        gainLabel_.setBounds (labels.removeFromLeft (labelW));
        qLabel_.setBounds (labels.removeFromLeft (labelW));
        slopeLabel_.setBounds (labels);

        typeBox_.setBounds (dials.removeFromLeft (dials.getWidth() / 5).reduced (4, 24));
        const int kw = dials.getWidth() / 4;
        freqKnob_.setBounds (dials.removeFromLeft (kw));
        gainKnob_.setBounds (dials.removeFromLeft (kw));
        qKnob_.setBounds (dials.removeFromLeft (kw));
        slopeBox_.setBounds (dials.reduced (4, 28));

        plotBounds_ = area.reduced (4, 8);
        refreshSlopeVisibility();
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
    void exclusiveSolo (int index, bool on)
    {
        if (apvts_ == nullptr || ! on) return;
        for (int i = 0; i < afterimage::constants::parametricEqBands; ++i)
        {
            if (i == index) continue;
            if (auto* p = apvts_->getParameter ("eq" + juce::String (i + 1) + "Solo"))
                if (p->getValue() > 0.5f)
                    p->setValueNotifyingHost (0.0f);
        }
    }

    void selectBand (int index)
    {
        selected_ = juce::jlimit (0, afterimage::constants::parametricEqBands - 1, index);
        bandButtons_[static_cast<size_t> (selected_)].setToggleState (true, juce::dontSendNotification);
        if (apvts_ == nullptr) return;

        typeAtt_.reset();
        const auto n = juce::String (selected_ + 1);
        freqKnob_.attachToParameter (*apvts_, "eq" + n + "Freq");
        gainKnob_.attachToParameter (*apvts_, "eq" + n + "Gain");
        qKnob_.attachToParameter (*apvts_, "eq" + n + "Q");
        typeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            *apvts_, "eq" + n + "Type", typeBox_);
        syncSlopeFromParam();
        refreshValueText();
        repaint();
    }

    void syncSlopeFromParam()
    {
        if (apvts_ == nullptr) return;
        updatingSlope_ = true;
        const auto n = juce::String (selected_ + 1);
        const bool x4 = apvts_->getRawParameterValue ("eq" + n + "X4")->load() > 0.5f;
        slopeBox_.setSelectedId (x4 ? 2 : 1, juce::dontSendNotification);
        updatingSlope_ = false;
    }

    void refreshSlopeVisibility()
    {
        const int type = typeBox_.getSelectedItemIndex(); // 0 LP, 1 HP, ...
        const bool show = (type == 0 || type == 1);
        slopeBox_.setVisible (show);
        slopeLabel_.setVisible (show);
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
    juce::Label typeLabel_, freqLabel_, gainLabel_, qLabel_, slopeLabel_;
    ChipToggle masterOn_;
    juce::ComboBox typeBox_;
    juce::ComboBox slopeBox_;
    std::array<juce::TextButton, afterimage::constants::parametricEqBands> bandButtons_ {};
    std::array<ChipToggle, afterimage::constants::parametricEqBands> onButtons_ {};
    std::array<ChipToggle, afterimage::constants::parametricEqBands> soloButtons_ {};
    AfterimageKnob freqKnob_, gainKnob_, qKnob_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> masterAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt_;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, afterimage::constants::parametricEqBands> onAtt_ {}, soloAtt_ {};

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    afterimage::ParametricEQ* eq_ = nullptr;
    juce::Rectangle<int> plotBounds_;
    std::array<float, afterimage::SpectrumProbe::kBins> bins_ {};
    int selected_ = 0;
    int dragging_ = -1;
    bool updatingSlope_ = false;
};
