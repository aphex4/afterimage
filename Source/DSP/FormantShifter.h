#pragma once

#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    Conventional formant tilt via three peaking resonators whose centres
    morph between low ("oo") and high ("ee") vowel-like positions.

    Parameter: 0 = Low formant, 0.5 = neutral, 1 = High formant.
    Sonic quality: musical tilt / vowel colour — not a research-grade shifter.
*/
class FormantShifter
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        juce::dsp::ProcessSpec spec {
            sampleRate_,
            (juce::uint32) juce::jmax (1, maxBlock),
            (juce::uint32) juce::jmax (1, numChannels)
        };

        for (auto& band : filters_)
            for (auto& f : band)
            {
                f.prepare (spec);
                f.reset();
            }

        amountSmoothed_ = 0.5f;
        updateFilters (0.5f);
    }

    void reset() noexcept
    {
        for (auto& band : filters_)
            for (auto& f : band)
                f.reset();
        amountSmoothed_ = 0.5f;
    }

    void setAmount (float amount01) noexcept
    {
        target_ = juce::jlimit (0.0f, 1.0f, amount01);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), 2);
        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::formantSmoothSec));

        for (int i = 0; i < numSamples; ++i)
        {
            amountSmoothed_ += coeff * (target_ - amountSmoothed_);
            if ((i & 63) == 0)
                updateFilters (amountSmoothed_);

            // Near centre: mostly dry (subtle)
            const float depth = std::abs (amountSmoothed_ - 0.5f) * 2.0f; // 0..1
            const float wet = depth * 0.85f;

            for (int ch = 0; ch < chans; ++ch)
            {
                float x = buffer.getSample (ch, i);
                float y = x;
                for (int b = 0; b < 3; ++b)
                    y = filters_[static_cast<size_t> (b)][static_cast<size_t> (ch)].processSample (y);
                buffer.setSample (ch, i, x * (1.0f - wet) + y * wet);
            }
        }
    }

private:
    void updateFilters (float amount01) noexcept
    {
        // Low formant (oo-ish) → High formant (ee-ish)
        const float t = amount01;
        const float f1 = juce::jmap (t, 280.0f, 350.0f);
        const float f2 = juce::jmap (t, 650.0f, 2200.0f);
        const float f3 = juce::jmap (t, 2200.0f, 3000.0f);
        const float g1 = juce::Decibels::decibelsToGain (juce::jmap (t, 5.0f, 3.0f));
        const float g2 = juce::Decibels::decibelsToGain (juce::jmap (t, 4.0f, 6.0f));
        const float g3 = juce::Decibels::decibelsToGain (juce::jmap (t, 2.0f, 5.0f));
        const float freqs[3] = { f1, f2, f3 };
        const float gains[3] = { g1, g2, g3 };
        const float qs[3] = { 4.5f, 5.0f, 4.0f };

        for (int b = 0; b < 3; ++b)
        {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sampleRate_, freqs[b], qs[b], gains[b]);
            for (int ch = 0; ch < 2; ++ch)
                *filters_[static_cast<size_t> (b)][static_cast<size_t> (ch)].coefficients = *coeffs;
        }
    }

    double sampleRate_ = 44100.0;
    float target_ = 0.5f;
    float amountSmoothed_ = 0.5f;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 3> filters_ {};
};

} // namespace afterimage
