#pragma once

#include "../Utilities/Constants.h"
#include "LogSmoother.h"

#include <cstdint>
#include <vector>

namespace afterimage
{

struct SpectralTailParams
{
    float rt60Seconds  = 3.0f;   // from Forget × Memory Length
    float hfDampRatio  = 0.22f;  // HF rt60 as a fraction of LF rt60
    float injectGain   = 1.0f;   // from Influence, ducked by transients
    float diffusion    = 0.0f;   // from Blur: 0 = tonal, 1 = fully random phase
    float shimmerCents = 0.0f;   // slow random detune of ghost phase advance
    float spectralDiffusion = 0.25f; // 0 = none, 1 = fully smeared each hop
    float diffusionOctaves  = 0.25f; // constant-Q width of the per-hop spread
    bool  freeze       = false;  // inject = 0, decay = 1
};

/**
    Per-bin spectral feedback accumulator with phase-vocoder ghost phase.

    Sized in prepare(); processHop is RT-safe (no alloc / locks / I/O).
*/
class SpectralTail
{
public:
    void prepare (int numBins, double sampleRate, int hopSize, int numChannels);
    void reset() noexcept;

    /** Advance one hop for one channel. */
    void processHop (int channelIndex,
                     const float* inputMagnitudes,
                     const float* inputPhases,
                     const SpectralTailParams& params) noexcept;

    [[nodiscard]] const float* getTailMagnitudes (int channelIndex) const noexcept;
    [[nodiscard]] const float* getTailPhases (int channelIndex) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

private:
    void recomputeDecayCoeffs (int channelIndex, float rt60Seconds, float hfDampRatio) noexcept;

    int numBins_ = 0;
    int hopSize_ = constants::hopSize;
    int numChannels_ = 2;
    double sampleRate_ = 44100.0;

    std::vector<std::vector<float>> tailMag_;
    std::vector<std::vector<float>> ghostPhase_;   // clean propagated phase
    std::vector<std::vector<float>> renderPhase_;  // ghost + per-hop diffusion offset
    std::vector<std::vector<float>> prevInputPhase_;
    std::vector<std::vector<float>> decayCoeff_;
    std::vector<std::vector<float>> shimmerLfo_;
    std::vector<std::vector<float>> tailOmega_; // held radians-per-hop
    std::vector<std::uint32_t> rng_;
    std::vector<float> cachedRt60_;
    std::vector<float> cachedHfDamp_;
    std::vector<bool> hasPrevPhase_;

    LogSmoother smoother_;
    std::vector<float> diffuseScratch_;
    std::vector<float> prefixScratch_;
};

} // namespace afterimage
