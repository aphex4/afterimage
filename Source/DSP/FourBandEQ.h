#pragma once

#include "Biquad.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    Four peaking bands, stereo-linked, prepare-sized. RT-safe process.
    Band Q is fixed (~0.85) — freq + gain are the user controls.
    Stack BiquadCoeffs only (no JUCE IIR::Coefficients heap).
*/
class FourBandEQ
{
public:
    void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));

        for (int b = 0; b < constants::eqBandsPerStage; ++b)
        {
            freqs_[static_cast<size_t> (b)] = constants::kDefaultEqFreqs[b];
            gainsDb_[static_cast<size_t> (b)] = 0.0f;
            updateBand (b);
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& band : states_)
            for (auto& s : band)
                s.reset();
    }

    void setBand (int index, float freqHz, float gainDb) noexcept
    {
        if (index < 0 || index >= constants::eqBandsPerStage)
            return;

        const float f = juce::jlimit (40.0f, 18000.0f, freqHz);
        const float g = juce::jlimit (-18.0f, 18.0f, gainDb);
        auto& fi = freqs_[static_cast<size_t> (index)];
        auto& gi = gainsDb_[static_cast<size_t> (index)];
        if (std::abs (fi - f) < 0.5f && std::abs (gi - g) < 0.05f)
            return;

        fi = f;
        gi = g;
        updateBand (index);
    }

    /** True when all band gains are effectively flat. */
    [[nodiscard]] bool isNeutral() const noexcept
    {
        for (int b = 0; b < constants::eqBandsPerStage; ++b)
            if (std::abs (gainsDb_[static_cast<size_t> (b)]) > 0.05f)
                return false;
        return true;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);

        for (int ch = 0; ch < chans; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float x = data[i];
                for (int b = 0; b < constants::eqBandsPerStage; ++b)
                    x = states_[static_cast<size_t> (b)][static_cast<size_t> (ch)]
                            .process (x, coeffs_[static_cast<size_t> (b)]);
                data[i] = x;
            }
        }
    }

    [[nodiscard]] float getBandFreq (int index) const noexcept
    {
        if (index < 0 || index >= constants::eqBandsPerStage)
            return 1000.0f;
        return freqs_[static_cast<size_t> (index)];
    }

    [[nodiscard]] float getBandGainDb (int index) const noexcept
    {
        if (index < 0 || index >= constants::eqBandsPerStage)
            return 0.0f;
        return gainsDb_[static_cast<size_t> (index)];
    }

private:
    void updateBand (int index) noexcept
    {
        constexpr float q = 0.85f;
        coeffs_[static_cast<size_t> (index)] = makePeak (
            sampleRate_,
            freqs_[static_cast<size_t> (index)],
            q,
            juce::Decibels::decibelsToGain (gainsDb_[static_cast<size_t> (index)]));
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    std::array<float, constants::eqBandsPerStage> freqs_ {};
    std::array<float, constants::eqBandsPerStage> gainsDb_ {};
    std::array<BiquadCoeffs, constants::eqBandsPerStage> coeffs_ {};
    std::array<std::array<BiquadState, 2>, constants::eqBandsPerStage> states_ {};
};

} // namespace afterimage
