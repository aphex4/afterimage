#include "STFTProcessor.h"

#include <cmath>

namespace afterimage
{

void STFTProcessor::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sampleRate_  = sampleRate;
    numChannels_ = std::max (1, numChannels);

    // Symmetric Hann: zeros at both ends → clean ring-buffer boundaries.
    for (int i = 0; i < fftSize; ++i)
    {
        window_[static_cast<size_t> (i)] =
            0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                     * static_cast<float> (i)
                                     / static_cast<float> (fftSize - 1)));
    }

    // Measure COLA of window² at a mid-window sample (hop = N/4 → constant).
    {
        double sum = 0.0;
        const int mid = fftSize / 2;
        for (int m = -fftSize; m <= fftSize; m += hopSize)
        {
            const int idx = mid + m;
            if (idx >= 0 && idx < fftSize)
            {
                const double w = static_cast<double> (window_[static_cast<size_t> (idx)]);
                sum += w * w;
            }
        }
        wolaScale_ = (sum > 1.0e-9) ? static_cast<float> (1.0 / sum) : 1.0f;
    }

    // One full ring before a written sample is consumed from the OLA buffer.
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
    // Oldest → newest fftSize samples from the input ring, analysis-windowed.
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (ch.pos + i) % fftSize;
        ch.fftBuf[static_cast<size_t> (i)] =
            ch.inRing[static_cast<size_t> (idx)] * window_[static_cast<size_t> (i)];
    }

    for (int i = fftSize; i < 2 * fftSize; ++i)
        ch.fftBuf[static_cast<size_t> (i)] = 0.0f;

    ch.fft.performRealOnlyForwardTransform (ch.fftBuf.data(), false);

    if (spectrumCallback_ != nullptr)
        spectrumCallback_ (spectrumUserData_, ch.fftBuf.data(), fftSize, channelIndex);

    ch.fft.performRealOnlyInverseTransform (ch.fftBuf.data()); // includes 1/N

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

    for (int c = 0; c < numCh; ++c)
    {
        auto& ch = *channels_[static_cast<size_t> (c)];
        float* data = buffer.getWritePointer (c);

        for (int s = 0; s < n; ++s)
        {
            ch.inRing[static_cast<size_t> (ch.pos)] = data[s];

            const float out = ch.outRing[static_cast<size_t> (ch.pos)] * wolaScale_;
            ch.outRing[static_cast<size_t> (ch.pos)] = 0.0f;
            data[s] = ch.primed ? out : 0.0f;

            ch.pos = (ch.pos + 1) % fftSize;

            if (++ch.count >= hopSize)
            {
                ch.count = 0;
                ch.primed = true;
                processFrame (ch, c);
            }
        }
    }
}

} // namespace afterimage
