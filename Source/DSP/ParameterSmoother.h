#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SpectralModes.h"
#include "../Utilities/Constants.h"

namespace afterimage
{

/**
    Sample-rate smoothers for mix/bypass/gain, plus hop-advanced spectral params.
*/
struct ParameterSmoother
{
    // Sample-smoothed (processor)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         mix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         bypassAmount;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         gainMatchAmount;

    // Frame-smoothed (engine advances by hopSize samples per FFT frame)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> influence;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> recallPosition;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> forget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> blur;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> transientPreserve;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> randomRecall;

    void prepareSampleSmoothers (double sampleRate)
    {
        mix.reset (sampleRate, constants::mixSmoothSec);
        mix.setCurrentAndTargetValue (1.0f);

        bypassAmount.reset (sampleRate, constants::bypassSmoothSec);
        bypassAmount.setCurrentAndTargetValue (0.0f);

        outputGain.reset (sampleRate, constants::gainSmoothSec);
        outputGain.setCurrentAndTargetValue (1.0f);

        gainMatchAmount.reset (sampleRate, constants::gainMatchSmoothSec);
        gainMatchAmount.setCurrentAndTargetValue (0.0f);
    }

    void prepareFrameSmoothers (double sampleRate)
    {
        // Initials match current APVTS defaults (Soft Shadow). prepareToPlay snaps
        // to live APVTS targets so the first frames never ramp from stale values.
        auto init = [sampleRate] (auto& s, float seconds, float initial)
        {
            s.reset (sampleRate, seconds);
            s.setCurrentAndTargetValue (initial);
        };

        init (influence,         constants::influenceSmoothSec, 0.40f);
        init (recallPosition,    constants::recallSmoothSec,    0.40f);
        init (forget,            constants::forgetSmoothSec,    0.25f);
        init (blur,              constants::blurSmoothSec,      0.12f);
        init (transientPreserve, 0.08f,                         0.35f);
        init (randomRecall,      constants::randomSmoothSec,    0.0f);
    }

    void setSpectralTargets (float influence01,
                             float recall01,
                             float forget01,
                             float blur01,
                             float transient01,
                             float random01) noexcept
    {
        influence.setTargetValue (influence01);
        recallPosition.setTargetValue (recall01);
        forget.setTargetValue (forget01);
        blur.setTargetValue (blur01);
        transientPreserve.setTargetValue (transient01);
        randomRecall.setTargetValue (random01);
    }

    /** Snap frame smoothers to their targets (startup / state load — no ramp). */
    void snapSpectralToTargets() noexcept
    {
        influence.setCurrentAndTargetValue (influence.getTargetValue());
        recallPosition.setCurrentAndTargetValue (recallPosition.getTargetValue());
        forget.setCurrentAndTargetValue (forget.getTargetValue());
        blur.setCurrentAndTargetValue (blur.getTargetValue());
        transientPreserve.setCurrentAndTargetValue (transientPreserve.getTargetValue());
        randomRecall.setCurrentAndTargetValue (randomRecall.getTargetValue());
    }

    /** Advance spectral smoothers by one STFT hop and return a ModeParams snapshot. */
    ModeParams snapSpectralParamsForHop (int hopSamples, bool freeze) noexcept
    {
        influence.skip (hopSamples);
        recallPosition.skip (hopSamples);
        forget.skip (hopSamples);
        blur.skip (hopSamples);
        transientPreserve.skip (hopSamples);
        randomRecall.skip (hopSamples);

        ModeParams p;
        p.influence         = influence.getCurrentValue();
        p.recallPosition    = recallPosition.getCurrentValue();
        p.forget            = forget.getCurrentValue();
        p.blur              = blur.getCurrentValue();
        p.transientPreserve = transientPreserve.getCurrentValue();
        p.randomRecall      = randomRecall.getCurrentValue();
        p.freeze            = freeze;
        return p;
    }
};

} // namespace afterimage
