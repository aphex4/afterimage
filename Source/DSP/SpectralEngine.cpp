#include "SpectralEngine.h"

namespace afterimage
{

void SpectralEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;

    stft_.prepare (sampleRate, maxBlockSize, numChannels);
    history_.prepare (sampleRate, stft_.getHopSize(), constants::memoryLengthMaxSec);
    modes_.prepare (constants::numBins);

    // Phase 2: identity spectrum (no callback). Phase 3+ installs a handler
    // that pushes history and runs Shadow / Erase / Merge.
    stft_.setSpectrumCallback (nullptr, nullptr);

    prepared_ = true;
    reset();
}

void SpectralEngine::reset()
{
    stft_.reset();
    history_.reset();
    modes_.reset();
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
    if (! prepared_)
        return;

    // Stash for future spectrum callback use (Phase 3+).
    pendingParams_ = params;
    pendingMode_ = mode;

    // Phase 2: transparent STFT — spectrum untouched, WOLA reconstruction only.
    stft_.process (buffer);

    juce::ignoreUnused (pendingParams_, pendingMode_);
}

} // namespace afterimage
