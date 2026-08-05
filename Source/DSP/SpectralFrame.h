#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace afterimage
{

/**
    One STFT analysis frame.

    Vectors are sized during prepareToPlay and must never be resized on the
    audio thread. Phase 1 stores the type only — the STFT engine arrives in
    Phase 2.
*/
struct SpectralFrame
{
    std::vector<float> magnitudes;
    std::vector<float> phases;

    float rms = 0.0f;
    float spectralCentroid = 0.0f;
    float transientStrength = 0.0f;

    std::uint64_t frameIndex = 0;

    void prepare (int numBins)
    {
        magnitudes.assign (static_cast<std::size_t> (numBins), 0.0f);
        phases.assign (static_cast<std::size_t> (numBins), 0.0f);
        rms = 0.0f;
        spectralCentroid = 0.0f;
        transientStrength = 0.0f;
        frameIndex = 0;
    }

    void clear()
    {
        std::fill (magnitudes.begin(), magnitudes.end(), 0.0f);
        std::fill (phases.begin(), phases.end(), 0.0f);
        rms = 0.0f;
        spectralCentroid = 0.0f;
        transientStrength = 0.0f;
        frameIndex = 0;
    }
};

} // namespace afterimage
