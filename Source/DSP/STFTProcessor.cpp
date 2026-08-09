#include "STFTProcessor.h"
#include "../Utilities/DebugSafety.h"

#include <cmath>

namespace afterimage
{

void STFTProcessor::buildWolaTable() noexcept
{
    // Per hop-phase COLA of window^2:
    //   for n in [0, hop):  sum_k w[n + k*hop]^2
    double minSum = 1.0e9;
    double maxSum = 0.0;

    for (int n = 0; n < hopSize; ++n)
    {
        double sum = 0.0;
        for (int idx = n; idx < fftSize; idx += hopSize)
        {
            const double w = static_cast<double> (window_[static_cast<size_t> (idx)]);
            sum += w * w;
        }

        jassert (sum > 1.0e-9);
        wolaScaleTable_[static_cast<size_t> (n)] = static_cast<float> (1.0 / sum);
        minSum = std::min (minSum, sum);
        maxSum = std::max (maxSum, sum);
    }

    const double mid = 0.5 * (minSum + maxSum);
    wolaMaxDeviation_ = (mid > 1.0e-9)
                            ? static_cast<float> ((maxSum - minSum) / mid)
                            : 0.0f;
}

void STFTProcessor::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sampleRate_  = sampleRate;
    numChannels_ = std::max (1, numChannels);

    // Periodic Hann — exact COLA at hop = N/R (symmetric Hann uses fftSize-1).
    for (int i = 0; i < fftSize; ++i)
    {
        window_[static_cast<size_t> (i)] =
            0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                     * static_cast<float> (i)
                                     / static_cast<float> (fftSize)));
    }

    buildWolaTable();
    latencySamples_ = fftSize;

    channels_.clear();
    channels_.reserve (static_cast<size_t> (numChannels_));
    for (int c = 0; c < numChannels_; ++c)
        channels_.push_back (std::make_unique<Channel>());

    prepared_ = true;
    reset();
}

void STFTProcessor::reset()
{
    hopCounter_ = 0;
    for (auto& ch : channels_)
        ch->clear();
}

void STFTProcessor::releaseResources()
{
    prepared_ = false;
}

void STFTProcessor::setSpectrumCallback (SpectrumCallback callback, void* userData) noexcept
{
    spectrumCallback_ = callback;
    spectrumUserData_ = userData;
}

void STFTProcessor::processFrame (Channel& ch, int channelIndex) noexcept
{
    jassert (prepared_);
    jassert (static_cast<int> (ch.fftBuf.size()) >= 2 * fftSize);

    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (ch.pos + i) % fftSize;
        ch.fftBuf[static_cast<size_t> (i)] =
            ch.inRing[static_cast<size_t> (idx)] * window_[static_cast<size_t> (i)];
    }

    for (int i = fftSize; i < 2 * fftSize; ++i)
        ch.fftBuf[static_cast<size_t> (i)] = 0.0f;

    ch.fft.performRealOnlyForwardTransform (ch.fftBuf.data(), false);

#if JUCE_DEBUG
    for (int i = 0; i < 2 * fftSize; ++i)
        jassert (std::isfinite (ch.fftBuf[static_cast<size_t> (i)]));
#endif

    if (spectrumCallback_ != nullptr)
        spectrumCallback_ (spectrumUserData_, ch.fftBuf.data(), fftSize, channelIndex);

    ch.fft.performRealOnlyInverseTransform (ch.fftBuf.data());

#if JUCE_DEBUG
    for (int i = 0; i < fftSize; ++i)
        jassert (std::isfinite (ch.fftBuf[static_cast<size_t> (i)]));
#endif

    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (ch.pos + i) % fftSize;
        ch.outRing[static_cast<size_t> (idx)] +=
            ch.fftBuf[static_cast<size_t> (i)] * window_[static_cast<size_t> (i)];
    }
}

void STFTProcessor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! prepared_)
        return;

    const int numCh = juce::jmin (buffer.getNumChannels(), static_cast<int> (channels_.size()));
    const int n     = buffer.getNumSamples();
    jassert (numCh >= 1);

    // Hoist write pointers once — allocation-free, cache-friendly.
    std::array<float*, 16> writePtrs {};
    jassert (numCh <= static_cast<int> (writePtrs.size()));
    for (int c = 0; c < numCh; ++c)
        writePtrs[static_cast<size_t> (c)] = buffer.getWritePointer (c);

    // Sample-outer / channel-inner so hops fire in time order across channels.
    // Shared hopCounter_ prevents L/R hop-parameter desync when host block > hop.
    for (int s = 0; s < n; ++s)
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = *channels_[static_cast<size_t> (c)];
            float* data = writePtrs[static_cast<size_t> (c)];

            ch.inRing[static_cast<size_t> (ch.pos)] = data[s];

            const int phase = ch.pos % hopSize;
            float out = ch.outRing[static_cast<size_t> (ch.pos)]
                      * wolaScaleTable_[static_cast<size_t> (phase)];
            ch.outRing[static_cast<size_t> (ch.pos)] = 0.0f;

            // Emergency ceiling only (~+6 dBFS). Not a mastering limiter.
            constexpr float kEmergencyCeiling = 2.0f;
            if (! std::isfinite (out))
            {
                out = 0.0f;
#if JUCE_DEBUG
                debug::safetyCounters().nonFiniteBins.fetch_add (1, std::memory_order_relaxed);
#endif
            }
            else if (out > kEmergencyCeiling || out < -kEmergencyCeiling)
            {
                out = juce::jlimit (-kEmergencyCeiling, kEmergencyCeiling, out);
#if JUCE_DEBUG
                debug::safetyCounters().emergencyCeilingHits.fetch_add (1, std::memory_order_relaxed);
#endif
            }

            data[s] = ch.primed ? out : 0.0f;

#if JUCE_DEBUG
            {
                const float peak = std::abs (data[s]);
                auto& counters = debug::safetyCounters();
                const float prev = counters.lastStagePeak.load (std::memory_order_relaxed);
                if (peak > prev)
                    counters.lastStagePeak.store (peak, std::memory_order_relaxed);
            }
#endif

            ch.pos = (ch.pos + 1) % fftSize;
        }

        if (++hopCounter_ >= hopSize)
        {
            hopCounter_ = 0;
            for (int c = 0; c < numCh; ++c)
            {
                channels_[static_cast<size_t> (c)]->primed = true;
                processFrame (*channels_[static_cast<size_t> (c)], c);
            }
        }
    }
}

} // namespace afterimage
