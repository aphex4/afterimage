#pragma once

#include "Biquad.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    Simple dynamic HF reduction — intensity knob only.
    Detects energy above ~6 kHz and ducks a high shelf.
    RT-safe: stack BiquadCoeffs only (no JUCE IIR::Coefficients heap).
*/
class DeEsser
{
public:
    void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        detectorCoeffs_ = makeHighPass (sampleRate_, 6000.0f, 0.707f);
        env_ = 0.0f;
        intensitySmoothed_ = 0.0f;
        enabled_ = false;
        updateShelf (0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            detectorState_[static_cast<size_t> (ch)].reset();
            shelfState_[static_cast<size_t> (ch)].reset();
        }
        env_ = 0.0f;
        intensitySmoothed_ = 0.0f;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }
    void setIntensity (float intensity01) noexcept
    {
        target_ = juce::jlimit (0.0f, 1.0f, intensity01);
    }

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! enabled_)
            return;

        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        const float smoothCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::deEsserSmoothSec));
        const float atk = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.003));
        const float rel = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.080));

        for (int i = 0; i < numSamples; ++i)
        {
            intensitySmoothed_ += smoothCoeff * (target_ - intensitySmoothed_);

            if (intensitySmoothed_ < 1.0e-4f)
            {
                for (int ch = 0; ch < chans; ++ch)
                    (void) detectorState_[static_cast<size_t> (ch)]
                               .process (buffer.getSample (ch, i), detectorCoeffs_);
                continue;
            }

            float det = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float d = detectorState_[static_cast<size_t> (ch)]
                                    .process (buffer.getSample (ch, i), detectorCoeffs_);
                det = juce::jmax (det, std::abs (d));
            }

            const float coeff = det > env_ ? atk : rel;
            env_ += coeff * (det - env_);

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
                x = shelfState_[static_cast<size_t> (ch)].process (x, shelfCoeffs_);
                buffer.setSample (ch, i, x);
            }
        }
    }

private:
    void updateShelf (float gainDb) noexcept
    {
        shelfCoeffs_ = makeHighShelf (sampleRate_, 5500.0f, 0.707f,
                                      juce::Decibels::decibelsToGain (gainDb));
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool enabled_ = false;
    float target_ = 0.0f;
    float intensitySmoothed_ = 0.0f;
    float env_ = 0.0f;
    BiquadCoeffs detectorCoeffs_ {};
    BiquadCoeffs shelfCoeffs_ {};
    std::array<BiquadState, 2> detectorState_ {};
    std::array<BiquadState, 2> shelfState_ {};
};

} // namespace afterimage
