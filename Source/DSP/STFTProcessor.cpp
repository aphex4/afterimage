#include "STFTProcessor.h"

#include <algorithm>

namespace afterimage
{

void STFTProcessor::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::max (1, numChannels);

    // Classic OLA latency for hop-based STFT is fftSize - hopSize when the
    // output is aligned to the centre of the analysis window. Exact value
    // will be confirmed when Phase 2 implements the FIFO/OLA path.
    latencySamples_ = constants::fftSize - constants::hopSize;
    prepared_ = true;
}

void STFTProcessor::reset()
{
    // TODO(Phase 2): clear input FIFOs, OLA accumulators, channel state.
}

void STFTProcessor::releaseResources()
{
    prepared_ = false;
}

void STFTProcessor::process (juce::AudioBuffer<float>& buffer, const FrameCallback& callback)
{
    // Phase 1: transparent pass-through. Spectral processing arrives in Phase 2.
    juce::ignoreUnused (buffer, callback, prepared_);
}

} // namespace afterimage
