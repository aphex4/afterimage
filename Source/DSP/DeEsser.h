#pragma once

#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace afterimage
{

/**
    Simple dynamic HF reduction — intensity knob only.
    Detects energy above ~6 kHz and ducks a high shelf. RT-safe.
*/
class DeEsser
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

        for (int ch = 0; ch < 2; ++ch)
        {
            detector_[static_cast<size_t> (ch)].prepare (spec);
            detector_[static_cast<size_t> (ch)].reset();
            shelf_[static_cast<size_t> (ch)].prepare (spec);
            shelf_[static_cast<size_t> (ch)].reset();
        }

        auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate_, 6000.0f, 0.707f);
        for (int ch = 0; ch < 2; ++ch)
            *detector_[static_cast<size_t> (ch)].coefficients = *hp;

        env_ = 0.0f;
        intensitySmoothed_ = 0.0f;
        updateShelf (0.0f);
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            detector_[static_cast<size_t> (ch)].reset();
            shelf_[static_cast<size_t> (ch)].reset();
        }
        env_ = 0.0f;
        intensitySmoothed_ = 0.0f;
    }

    void setIntensity (float intensity01) noexcept
    {
        target_ = juce::jlimit (0.0f, 1.0f, intensity01);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), 2);
        const float smoothCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::deEsserSmoothSec));
        const float atk = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.003));
        const float rel = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.080));

        for (int i = 0; i < numSamples; ++i)
        {
            intensitySmoothed_ += smoothCoeff * (target_ - intensitySmoothed_);

            if (intensitySmoothed_ < 1.0e-4f)
            {
                // Still advance detectors lightly to avoid clicks when engaging
                for (int ch = 0; ch < chans; ++ch)
                    detector_[static_cast<size_t> (ch)].processSample (buffer.getSample (ch, i));
                continue;
            }

            float det = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float d = detector_[static_cast<size_t> (ch)].processSample (buffer.getSample (ch, i));
                det = juce::jmax (det, std::abs (d));
            }

            const float coeff = det > env_ ? atk : rel;
            env_ += coeff * (det - env_);

            // Intensity raises sensitivity and max cut
            const float thresh = juce::jmap (intensitySmoothed_, 0.12f, 0.02f);
            const float maxCutDb = juce::jmap (intensitySmoothed_, 0.0f, 12.0f);
            float grDb = 0.0f;
            if (env_ > thresh)
            {
                const float over = (env_ - thresh) / juce::jmax (1.0e-6f, thresh);
                grDb = -juce::jmin (maxCutDb, over * maxCutDb);
            }

            if ((i & 31) == 0)
                updateShelf (grDb);

            for (int ch = 0; ch < chans; ++ch)
            {
                float x = buffer.getSample (ch, i);
                x = shelf_[static_cast<size_t> (ch)].processSample (x);
                buffer.setSample (ch, i, x);
            }
        }
    }

private:
    void updateShelf (float gainDb) noexcept
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate_, 5500.0f, 0.707f, juce::Decibels::decibelsToGain (gainDb));
        for (int ch = 0; ch < 2; ++ch)
            *shelf_[static_cast<size_t> (ch)].coefficients = *coeffs;
    }

    double sampleRate_ = 44100.0;
    float target_ = 0.0f;
    float intensitySmoothed_ = 0.0f;
    float env_ = 0.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> detector_ {};
    std::array<juce::dsp::IIR::Filter<float>, 2> shelf_ {};
};

} // namespace afterimage
