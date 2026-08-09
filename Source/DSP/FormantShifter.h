#pragma once

#include "Biquad.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    Conventional formant tilt via three peaking resonators whose centres
    morph between low ("oo") and high ("ee") vowel-like positions.

    Parameter: 0 = Low formant, 0.5 = neutral, 1 = High formant.
    RT-safe: stack BiquadCoeffs only (no JUCE IIR::Coefficients heap).
*/
class FormantShifter
{
public:
    void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        amountSmoothed_ = 0.5f;
        enabled_ = false;
        updateFilters (0.5f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& band : states_)
            for (auto& s : band)
                s.reset();
        amountSmoothed_ = 0.5f;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }

    void setAmount (float amount01) noexcept
    {
        target_ = juce::jlimit (0.0f, 1.0f, amount01);
    }

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! enabled_)
            return;

        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::formantSmoothSec));

        for (int i = 0; i < numSamples; ++i)
        {
            amountSmoothed_ += coeff * (target_ - amountSmoothed_);
            if ((i & 63) == 0)
                updateFilters (amountSmoothed_);

            const float depth = std::abs (amountSmoothed_ - 0.5f) * 2.0f;
            const float wet = depth * 0.85f;
            if (wet < 1.0e-5f)
            {
                // Keep filter state warm near centre without altering audio.
                for (int ch = 0; ch < chans; ++ch)
                {
                    float x = buffer.getSample (ch, i);
                    for (int b = 0; b < 3; ++b)
                        (void) states_[static_cast<size_t> (b)][static_cast<size_t> (ch)]
                                   .process (x, coeffs_[static_cast<size_t> (b)]);
                }
                continue;
            }

            for (int ch = 0; ch < chans; ++ch)
            {
                float x = buffer.getSample (ch, i);
                float y = x;
                for (int b = 0; b < 3; ++b)
                    y = states_[static_cast<size_t> (b)][static_cast<size_t> (ch)]
                            .process (y, coeffs_[static_cast<size_t> (b)]);
                buffer.setSample (ch, i, x * (1.0f - wet) + y * wet);
            }
        }
    }

private:
    void updateFilters (float amount01) noexcept
    {
        const float t = amount01;
        const float freqs[3] = {
            juce::jmap (t, 280.0f, 350.0f),
            juce::jmap (t, 650.0f, 2200.0f),
            juce::jmap (t, 2200.0f, 3000.0f)
        };
        const float gains[3] = {
            juce::Decibels::decibelsToGain (juce::jmap (t, 5.0f, 3.0f)),
            juce::Decibels::decibelsToGain (juce::jmap (t, 4.0f, 6.0f)),
            juce::Decibels::decibelsToGain (juce::jmap (t, 2.0f, 5.0f))
        };
        const float qs[3] = { 4.5f, 5.0f, 4.0f };

        for (int b = 0; b < 3; ++b)
            coeffs_[static_cast<size_t> (b)] = makePeak (sampleRate_, freqs[b], qs[b], gains[b]);
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool enabled_ = false;
    float target_ = 0.5f;
    float amountSmoothed_ = 0.5f;
    std::array<BiquadCoeffs, 3> coeffs_ {};
    std::array<std::array<BiquadState, 2>, 3> states_ {};
};

} // namespace afterimage
