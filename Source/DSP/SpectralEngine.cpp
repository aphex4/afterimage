#include "SpectralEngine.h"

namespace afterimage
{

void SpectralEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;

    stft_.prepare (sampleRate, maxBlockSize, numChannels);
    history_.prepare (sampleRate, stft_.getHopSize(), constants::memoryLengthMaxSec);
    modes_.prepare (constants::numBins);
    dryWet_.prepare (sampleRate);

    prepared_ = true;
    reset();
}

void SpectralEngine::reset()
{
    stft_.reset();
    history_.reset();
    modes_.reset();
    dryWet_.reset();
}

void SpectralEngine::releaseResources()
{
    stft_.releaseResources();
    prepared_ = false;
}

void SpectralEngine::process (juce::AudioBuffer<float>& buffer,
                              const ModeParams& params,
                              SpectralMode mode)
{
    // Phase 1: engine is prepared but not yet inserted into the audio path.
    juce::ignoreUnused (buffer, params, mode, prepared_);

    // TODO(Phase 2): stft_.process(...) extracting frames, pushing history,
    // running modes_, reconstructing via OLA.
}

} // namespace afterimage
