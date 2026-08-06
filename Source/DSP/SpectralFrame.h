#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace afterimage
{

/**
    One STFT analysis frame.

    Vectors are sized during prepare() and must never be resized on the
    audio thread.
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
        clearMetadata();
    }

    void clear()
    {
        std::fill (magnitudes.begin(), magnitudes.end(), 0.0f);
        std::fill (phases.begin(), phases.end(), 0.0f);
        clearMetadata();
    }

    void clearMetadata() noexcept
    {
        rms = 0.0f;
        spectralCentroid = 0.0f;
        transientStrength = 0.0f;
        frameIndex = 0;
    }

    /** RT-safe copy into a same-sized destination (no reallocation). */
    void copyFrom (const SpectralFrame& other) noexcept
    {
        const auto n = std::min (magnitudes.size(), other.magnitudes.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            magnitudes[i] = other.magnitudes[i];
            phases[i] = other.phases[i];
        }
        rms = other.rms;
        spectralCentroid = other.spectralCentroid;
        transientStrength = other.transientStrength;
        frameIndex = other.frameIndex;
    }

    /**
        Fill magnitude/phase from a juce real-only interleaved FFT buffer.
        Uses bins [0 .. numBins-1] where numBins = fftSize/2 + 1.
        Does not resize. Leaves metadata for the caller to set.
    */
    void fillFromInterleavedFFT (const float* interleaved, int fftSize) noexcept
    {
        const int numBins = static_cast<int> (magnitudes.size());
        const int maxBin = std::min (numBins - 1, fftSize / 2);

        for (int k = 0; k <= maxBin; ++k)
        {
            const float re = interleaved[2 * k];
            const float im = interleaved[2 * k + 1];
            magnitudes[static_cast<std::size_t> (k)] = std::sqrt (re * re + im * im);
            phases[static_cast<std::size_t> (k)] = std::atan2 (im, re);
        }

        for (int k = maxBin + 1; k < numBins; ++k)
        {
            magnitudes[static_cast<std::size_t> (k)] = 0.0f;
            phases[static_cast<std::size_t> (k)] = 0.0f;
        }
    }

    void computeRmsAndCentroid (double sampleRate, int fftSize) noexcept
    {
        const int numBins = static_cast<int> (magnitudes.size());
        double energy = 0.0;
        double weightedFreq = 0.0;
        double magSum = 0.0;

        for (int k = 0; k < numBins; ++k)
        {
            const double m = static_cast<double> (magnitudes[static_cast<std::size_t> (k)]);
            const double e = m * m;
            energy += e;
            magSum += m;
            const double freqHz = (static_cast<double> (k) * sampleRate) / static_cast<double> (fftSize);
            weightedFreq += freqHz * m;
        }

        rms = static_cast<float> (std::sqrt (energy / std::max (1, numBins)));
        spectralCentroid = (magSum > 1.0e-12)
                               ? static_cast<float> (weightedFreq / magSum)
                               : 0.0f;
    }
};

} // namespace afterimage
