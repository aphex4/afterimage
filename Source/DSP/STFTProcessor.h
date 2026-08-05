#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../Utilities/Constants.h"

#include <functional>

namespace afterimage
{

/**
    Overlap-add STFT processor.

    Phase 1: prepared but unused — processBlock passes audio through.
    Phase 2: FIFO → window → FFT → callback → IFFT → OLA with correct latency.
*/
class STFTProcessor
{
public:
    using FrameCallback = std::function<void (float* realImagInterleaved, int fftSize)>;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    /** Phase 2 entry point. Phase 1 leaves the buffer unchanged. */
    void process (juce::AudioBuffer<float>& buffer, const FrameCallback& callback);

    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples_; }
    [[nodiscard]] int getFftSize() const noexcept { return constants::fftSize; }
    [[nodiscard]] int getHopSize() const noexcept { return constants::hopSize; }

private:
    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    int latencySamples_ = 0;
    bool prepared_ = false;
};

} // namespace afterimage
