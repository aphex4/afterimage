#pragma once

#include "FourBandEQ.h"
#include "SpectrumProbe.h"
#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace afterimage
{

enum class ReverbType
{
    Spring = 0,
    Hall,
    Room
};

/**
    Conventional algorithmic reverb (JUCE dsp::Reverb) with Pre/Post 4-band EQ.

    Routing: dry ‖ (Pre-EQ → Verb → Post-EQ) → wet mix.
    wet≈0 (and settling) is bit-identical pass-through of the input.
    Pre-EQ never colors the dry path.
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

        preEq_.prepare (sampleRate_, maxBlock_, numChannels_);
        postEq_.prepare (sampleRate_, maxBlock_, numChannels_);
        preProbe_.prepare (sampleRate_, maxBlock_);
        postProbe_.prepare (sampleRate_, maxBlock_);

        dryScratch_.setSize (numChannels_, maxBlock_, false, true, true);
        wetScratch_.setSize (numChannels_, maxBlock_, false, true, true);
        wetAmount_ = 0.0f;
        wetTarget_ = 0.0f;
        enabled_ = false;
        applyType (ReverbType::Hall);
    }

    void reset() noexcept
    {
        reverb_.reset();
        preEq_.reset();
        postEq_.reset();
        preProbe_.reset();
        postProbe_.reset();
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

    void setPreBand (int index, float freqHz, float gainDb) noexcept
    {
        preEq_.setBand (index, freqHz, gainDb);
    }

    void setPostBand (int index, float freqHz, float gainDb) noexcept
    {
        postEq_.setBand (index, freqHz, gainDb);
    }

    FourBandEQ& getPreEq() noexcept { return preEq_; }
    FourBandEQ& getPostEq() noexcept { return postEq_; }
    const SpectrumProbe& getPreProbe() const noexcept { return preProbe_; }
    const SpectrumProbe& getPostProbe() const noexcept { return postProbe_; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), dryScratch_.getNumChannels());
        if (numSamples <= 0 || chans <= 0)
            return;

        if (! enabled_ && wetAmount_ < 1.0e-5f)
        {
            wetAmount_ = 0.0f;
            return;
        }

        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::reverbWetSmoothSec));

        // Fast identity when disabled or fully dry and settled.
        const float target = enabled_ ? wetTarget_ : 0.0f;
        if (! enabled_ || (target < 1.0e-5f && wetAmount_ < 1.0e-5f))
        {
            wetAmount_ = 0.0f;
            return;
        }

        // Preserve true dry (pre any EQ).
        for (int ch = 0; ch < chans; ++ch)
            dryScratch_.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        // Wet branch: Pre-EQ → Reverb → Post-EQ
        for (int ch = 0; ch < chans; ++ch)
            wetScratch_.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        preEq_.process (wetScratch_);

        {
            const float* l = wetScratch_.getReadPointer (0);
            const float* r = chans > 1 ? wetScratch_.getReadPointer (1) : nullptr;
            preProbe_.process (l, r, numSamples);
        }

        juce::dsp::Reverb::Parameters rp = baseParams_;
        rp.dryLevel = 0.0f;
        rp.wetLevel = 1.0f;
        reverb_.setParameters (rp);

        {
            juce::dsp::AudioBlock<float> block (wetScratch_);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            reverb_.process (ctx);
        }

        postEq_.process (wetScratch_);

        {
            const float* l = wetScratch_.getReadPointer (0);
            const float* r = chans > 1 ? wetScratch_.getReadPointer (1) : nullptr;
            postProbe_.process (l, r, numSamples);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            wetAmount_ += coeff * (target - wetAmount_);
            const float w = wetAmount_;
            const float d = 1.0f - w;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float dry = dryScratch_.getSample (ch, i);
                const float wet = wetScratch_.getSample (ch, i);
                buffer.setSample (ch, i, dry * d + wet * w);
            }
        }
    }

private:
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
    ReverbType type_ = ReverbType::Hall;
    float wetTarget_ = 0.0f;
    float wetAmount_ = 0.0f;
    juce::dsp::Reverb::Parameters baseParams_ {};
    juce::dsp::Reverb reverb_;
    FourBandEQ preEq_;
    FourBandEQ postEq_;
    SpectrumProbe preProbe_;
    SpectrumProbe postProbe_;
    juce::AudioBuffer<float> dryScratch_;
    juce::AudioBuffer<float> wetScratch_;
};

} // namespace afterimage
