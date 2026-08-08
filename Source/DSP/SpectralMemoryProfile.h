#pragma once

#include "SpectralHistoryBuffer.h"
#include "../Utilities/Constants.h"

#include <vector>

namespace afterimage
{

/** Options for building a short stabilized spectral memory profile. */
struct MemoryProfileOptions
{
    float windowMs = constants::memoryProfileWindowMs;
    bool  applyStability = true;
    float varianceScale = 1.25f;
    /** Gaussian σ as a fraction of the half-window (in frames). */
    float sigmaFraction = 0.42f;
};

/**
    Weighted temporal spectral profile over a short history neighborhood.

    Replaces single-frame recall: magnitudes are aggregated across ~150–250 ms
    with Gaussian temporal weights and optional variance-based stability.

    All storage is prepared up-front — build/copy/lerp are audio-thread safe.
*/
class SpectralMemoryProfile
{
public:
    void prepare (int numBins,
                  double sampleRate,
                  int hopSize,
                  float maxWindowMs = constants::memoryProfileMaxWindowMs);
    void reset() noexcept;
    void clear() noexcept;

    /**
        Build a profile centered on recallAge01 (0 = newest in active window).
        Clamps the temporal window at history boundaries.
    */
    void buildFromHistory (const SpectralHistoryBuffer& history,
                           float recallAge01,
                           const MemoryProfileOptions& options = {}) noexcept;

    /**
        Freeze capture: stabilize the most recent ~windowMs, biased slightly
        toward the past (no future frames). Center weight near ~30% of the window age.
    */
    void captureRecent (const SpectralHistoryBuffer& history,
                        float windowMs = constants::memoryProfileWindowMs,
                        const MemoryProfileOptions& options = {}) noexcept;

    /** RT-safe copy into a same-sized prepared profile. */
    void copyFrom (const SpectralMemoryProfile& other) noexcept;

    /** dest = (1-t)*a + t*b per bin; energy recomputed. */
    void lerpFrom (const SpectralMemoryProfile& a,
                   const SpectralMemoryProfile& b,
                   float t01) noexcept;

    [[nodiscard]] const float* getMagnitudes() const noexcept { return magnitudes_.data(); }
    [[nodiscard]] float* getMagnitudesWritable() noexcept { return magnitudes_.data(); }
    [[nodiscard]] const float* getStabilityWeights() const noexcept { return stability_.data(); }
    [[nodiscard]] float getEnergy() const noexcept { return energy_; }
    [[nodiscard]] int getNumBins() const noexcept { return numBins_; }
    [[nodiscard]] int getFramesUsed() const noexcept { return framesUsed_; }
    [[nodiscard]] int getMaxWindowFrames() const noexcept { return maxWindowFrames_; }
    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

    /** Gaussian temporal weight for tests / callers. */
    [[nodiscard]] static float gaussianWeight (float ageOffsetFrames, float sigmaFrames) noexcept;

private:
    void accumulateFrame (const SpectralFrame& frame, float weight) noexcept;
    void finalize (bool applyStability, float varianceScale) noexcept;
    void recomputeEnergy() noexcept;

    int numBins_ = 0;
    int hopSize_ = constants::hopSize;
    double sampleRate_ = 44100.0;
    int maxWindowFrames_ = 1;

    std::vector<float> magnitudes_;
    std::vector<float> weightSum_;       // per-bin weight (usually uniform)
    std::vector<float> weightedSum_;     // Σ w * mag
    std::vector<float> weightedSumSq_;   // Σ w * mag²
    std::vector<float> stability_;

    float energy_ = 0.0f;
    int framesUsed_ = 0;
    float totalWeight_ = 0.0f;
};

} // namespace afterimage
