#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace afterimage
{

/**
    Thin wrappers around juce::SmoothedValue with the Phase 1 smoothing times.
    Kept as a utility so the processor does not scatter reset/setTarget calls.
*/
struct ParameterSmoother
{
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         influence;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         recallPosition;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         forget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         blur;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         transientPreserve;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         randomRecall;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         mix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         bypassAmount; // 0 = processed, 1 = bypassed

    void prepare (double sampleRate)
    {
        auto resetLinear = [sampleRate] (auto& s, float seconds, float initial)
        {
            s.reset (sampleRate, seconds);
            s.setCurrentAndTargetValue (initial);
        };

        resetLinear (influence,         0.06f, 0.5f);
        resetLinear (recallPosition,    0.18f, 0.45f);
        resetLinear (forget,            0.08f, 0.35f);
        resetLinear (blur,              0.08f, 0.15f);
        resetLinear (transientPreserve, 0.08f, 0.5f);
        resetLinear (randomRecall,      0.15f, 0.0f);
        resetLinear (mix,               0.03f, 1.0f);
        resetLinear (bypassAmount,      0.02f, 0.0f);

        outputGain.reset (sampleRate, 0.03);
        outputGain.setCurrentAndTargetValue (1.0f);
    }

    void skip()
    {
        influence.skip (1);
        recallPosition.skip (1);
        forget.skip (1);
        blur.skip (1);
        transientPreserve.skip (1);
        randomRecall.skip (1);
        mix.skip (1);
        outputGain.skip (1);
        bypassAmount.skip (1);
    }
};

} // namespace afterimage
