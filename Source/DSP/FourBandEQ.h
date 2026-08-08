#pragma once

#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    Four peaking bands, stereo-linked, prepare-sized. RT-safe process.
    Band Q is fixed (~0.85) — freq + gain are the user controls.
*/
class FourBandEQ
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, numChannels);
        juce::dsp::ProcessSpec spec {
            sampleRate_,
            (juce::uint32) juce::jmax (1, maxBlock),
            (juce::uint32) numChannels_
        };

        for (int b = 0; b < constants::eqBandsPerStage; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                filters_[static_cast<size_t> (b)][static_cast<size_t> (ch)].prepare (spec);
                filters_[static_cast<size_t> (b)][static_cast<size_t> (ch)].reset();
            }
            freqs_[static_cast<size_t> (b)] = constants::kDefaultEqFreqs[b];
            gainsDb_[static_cast<size_t> (b)] = 0.0f;
            updateBand (b);
        }
    }

    void reset() noexcept
    {
        for (auto& band : filters_)
            for (auto& f : band)
                f.reset();
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

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), 2);

        for (int ch = 0; ch < chans; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int b = 0; b < constants::eqBandsPerStage; ++b)
            {
                auto& filter = filters_[static_cast<size_t> (b)][static_cast<size_t> (ch)];
                for (int i = 0; i < numSamples; ++i)
                    data[i] = filter.processSample (data[i]);
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
        const float f = freqs_[static_cast<size_t> (index)];
        const float g = gainsDb_[static_cast<size_t> (index)];
        constexpr float q = 0.85f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate_, f, q, juce::Decibels::decibelsToGain (g));
        for (int ch = 0; ch < 2; ++ch)
            *filters_[static_cast<size_t> (index)][static_cast<size_t> (ch)].coefficients = *coeffs;
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    std::array<float, constants::eqBandsPerStage> freqs_ {};
    std::array<float, constants::eqBandsPerStage> gainsDb_ {};
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, constants::eqBandsPerStage> filters_ {};
};

} // namespace afterimage
