#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace afterimage
{

/**
    Latency-compensated dry/wet mixer with equal-power crossfade.

    Dry is delayed by the STFT latency so dry and wet stay time-aligned
    (avoids comb filtering on intermediate mix values).
*/
class DryWetMixer
{
public:
    void prepare (int numChannels, int maxBlockSize, int delaySamples)
    {
        numChannels_  = std::max (1, numChannels);
        maxBlockSize_ = std::max (1, maxBlockSize);
        delaySamples_ = std::max (0, delaySamples);

        const int delayBufferSize = delaySamples_ + maxBlockSize_ + 1;
        delayBuffer_.setSize (numChannels_, delayBufferSize, false, true, true);
        delayBuffer_.clear();
        writePos_ = 0;
        prepared_ = true;
    }

    void reset()
    {
        delayBuffer_.clear();
        writePos_ = 0;
    }

    void releaseResources()
    {
        prepared_ = false;
    }

    [[nodiscard]] int getDelaySamples() const noexcept { return delaySamples_; }

    /** Delay the dry signal into delayedDry (must be pre-sized). */
    void processDryDelay (const juce::AudioBuffer<float>& dryIn,
                          juce::AudioBuffer<float>& delayedDry) noexcept
    {
        if (! prepared_)
        {
            delayedDry.makeCopyOf (dryIn, true);
            return;
        }

        const int numSamples  = dryIn.getNumSamples();
        const int numChannels = juce::jmin (dryIn.getNumChannels(),
                                   juce::jmin (delayedDry.getNumChannels(),
                                   juce::jmin (delayBuffer_.getNumChannels(), numChannels_)));
        const int delaySize = delayBuffer_.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in  = dryIn.getReadPointer (ch);
            float* out       = delayedDry.getWritePointer (ch);
            float* delay     = delayBuffer_.getWritePointer (ch);

            int w = writePos_;

            for (int i = 0; i < numSamples; ++i)
            {
                delay[w] = in[i];

                int r = w - delaySamples_;
                if (r < 0)
                    r += delaySize;

                out[i] = delay[r];
                w = (w + 1) % delaySize;
            }
        }

        writePos_ = (writePos_ + numSamples) % delaySize;
    }

    /** mix01 in [0,1]: 0 = dry, 1 = wet. Equal-power crossfade into wetBuffer. */
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

private:
    juce::AudioBuffer<float> delayBuffer_;
    int numChannels_  = 2;
    int maxBlockSize_ = 512;
    int delaySamples_ = 0;
    int writePos_     = 0;
    bool prepared_    = false;
};

} // namespace afterimage
