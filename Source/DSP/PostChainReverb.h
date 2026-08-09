#pragma once

#include "Biquad.h"
#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace afterimage
{

enum class ReverbType
{
    Spring = 0, // APVTS / preset index 0
    Hall,       // 1
    Room        // 2
};

/**
    Simplified algorithmic reverb.

    Controls: ON, TYPE (Room/Hall/Spring), MIX, SAFE BASS.
    SAFE BASS = ~24 dB/oct HPF (~125 Hz) on wet return only; dry stays full-range.
    Wet=0 / OFF = identity. Equal-power dry/wet mix.
*/
class PostChainReverb
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        maxBlock_ = juce::jmax (1, maxBlock);
        numChannels_ = juce::jmax (1, numChannels);

        juce::dsp::ProcessSpec spec {
            sampleRate_,
            (juce::uint32) maxBlock_,
            (juce::uint32) numChannels_
        };
        reverb_.prepare (spec);
        reverb_.reset();

        dryScratch_.setSize (numChannels_, maxBlock_, false, true, true);
        wetScratch_.setSize (numChannels_, maxBlock_, false, true, true);

        rebuildSafeBass();
        wetAmount_ = 0.0f;
        wetTarget_ = 0.0f;
        enabled_ = false;
        safeBass_ = false;
        applyType (ReverbType::Hall);
        reset();
    }

    void reset() noexcept
    {
        reverb_.reset();
        for (auto& ch : hpfStates_)
            for (auto& s : ch)
                s.reset();
        wetAmount_ = 0.0f;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    void setType (ReverbType type) noexcept
    {
        if (type == type_)
            return;
        type_ = type;
        applyType (type_);
    }

    void setWet (float wet01) noexcept
    {
        wetTarget_ = juce::jlimit (0.0f, 1.0f, wet01);
    }

    void setSafeBass (bool on) noexcept { safeBass_ = on; }
    [[nodiscard]] bool isSafeBass() const noexcept { return safeBass_; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), dryScratch_.getNumChannels());
        if (numSamples <= 0 || chans <= 0)
            return;

        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::reverbWetSmoothSec));
        const float target = enabled_ ? wetTarget_ : 0.0f;

        if (! enabled_ || (target < 1.0e-5f && wetAmount_ < 1.0e-5f))
        {
            wetAmount_ = 0.0f;
            return;
        }

        for (int ch = 0; ch < chans; ++ch)
        {
            dryScratch_.copyFrom (ch, 0, buffer, ch, 0, numSamples);
            wetScratch_.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        }

        {
            juce::dsp::Reverb::Parameters rp = baseParams_;
            rp.dryLevel = 0.0f;
            rp.wetLevel = 1.0f;
            reverb_.setParameters (rp);
            juce::dsp::AudioBlock<float> block (wetScratch_);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            reverb_.process (ctx);
        }

        if (safeBass_)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                float* data = wetScratch_.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    float y = data[i];
                    for (int s = 0; s < 2; ++s)
                        y = hpfStates_[static_cast<size_t> (ch)][static_cast<size_t> (s)]
                                .process (y, hpfCoeffs_[static_cast<size_t> (s)]);
                    data[i] = y;
                }
            }
        }

        for (int i = 0; i < numSamples; ++i)
        {
            wetAmount_ += coeff * (target - wetAmount_);
            const float w = wetAmount_;
            // Equal-power-ish dry/wet
            const float dryG = std::cos (w * juce::MathConstants<float>::halfPi);
            const float wetG = std::sin (w * juce::MathConstants<float>::halfPi);
            for (int ch = 0; ch < chans; ++ch)
            {
                const float dry = dryScratch_.getSample (ch, i);
                const float wet = wetScratch_.getSample (ch, i);
                buffer.setSample (ch, i, dry * dryG + wet * wetG);
            }
        }
    }

private:
    void rebuildSafeBass() noexcept
    {
        // Two cascaded 12 dB/oct HPFs ~ 24 dB/oct at ~125 Hz
        hpfCoeffs_[0] = makeHighPass (sampleRate_, 125.0f, 0.707f);
        hpfCoeffs_[1] = makeHighPass (sampleRate_, 125.0f, 0.707f);
    }

    void applyType (ReverbType type) noexcept
    {
        juce::dsp::Reverb::Parameters p;
        p.width = 1.0f;
        p.freezeMode = 0.0f;
        p.dryLevel = 0.0f;
        p.wetLevel = 1.0f;

        switch (type)
        {
            case ReverbType::Spring:
                p.roomSize = 0.28f;
                p.damping = 0.55f;
                break;
            case ReverbType::Room:
                p.roomSize = 0.45f;
                p.damping = 0.40f;
                break;
            case ReverbType::Hall:
            default:
                p.roomSize = 0.78f;
                p.damping = 0.28f;
                break;
        }
        baseParams_ = p;
        reverb_.setParameters (baseParams_);
    }

    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;
    int numChannels_ = 2;
    bool enabled_ = false;
    bool safeBass_ = false;
    ReverbType type_ = ReverbType::Hall;
    float wetTarget_ = 0.0f;
    float wetAmount_ = 0.0f;
    juce::dsp::Reverb::Parameters baseParams_ {};
    juce::dsp::Reverb reverb_;
    std::array<BiquadCoeffs, 2> hpfCoeffs_ {};
    std::array<std::array<BiquadState, 2>, 2> hpfStates_ {};
    juce::AudioBuffer<float> dryScratch_;
    juce::AudioBuffer<float> wetScratch_;
};

} // namespace afterimage
