#pragma once

#include <juce_dsp/juce_dsp.h>

namespace afterimage
{

/**
    Header-only dry/wet mixer with equal-power crossfade.

    Phase 1 applies mix without latency compensation. Phase 2+ must delay
    the dry path by the STFT latency so dry and wet align.
*/
class DryWetMixer
{
public:
    void prepare (double /*sampleRate*/)
    {
        // Reserved for dry-delay buffer allocation in Phase 2.
    }

    void reset() {}

    /** mix01 in [0,1]: 0 = dry, 1 = wet. Equal-power crossfade. */
    static void applyEqualPower (juce::AudioBuffer<float>& wetBuffer,
                                 const juce::AudioBuffer<float>& dryBuffer,
                                 float mix01) noexcept
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, mix01);
        const float dryGain = std::cos (clamped * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (clamped * juce::MathConstants<float>::halfPi);

        const int numChannels = juce::jmin (wetBuffer.getNumChannels(), dryBuffer.getNumChannels());
        const int numSamples  = juce::jmin (wetBuffer.getNumSamples(), dryBuffer.getNumSamples());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = wetBuffer.getWritePointer (ch);
            const auto* dry = dryBuffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] * dryGain + wet[i] * wetGain;
        }
    }
};

} // namespace afterimage
