#pragma once

#include "../Utilities/Constants.h"
#include "LogSmoother.h"

#include <cstdint>
#include <vector>

namespace afterimage
{

struct SpectralBlurParams
{
    float timeSmearMs      = 300.0f;  // temporal smearing window (Blur-driven)
    float freqSmearOctaves = 0.15f;   // constant-Q frequency spread (Blur-driven)
    float phaseScatter     = 0.5f;    // 0 = coherent, 1 = fully randomised per hop
    float memoryBlend      = 0.0f;    // 0 = blur live input, 1 = blur recalled memory
    bool  freeze           = false;   // lock EMA magnitudes; phase keeps advancing
};

/**
    Per-bin spectral blur: power-domain temporal EMA, narrow constant-Q freq
    smear, held-omega phase propagation, and render-time phase scatter.

    Sized in prepare(); processHop is RT-safe (no alloc / locks / I/O).
*/
class SpectralBlur
{
public:
    void prepare (int numBins, double sampleRate, int hopSize, int numChannels);
    void reset() noexcept;

    /** Advance one hop for one channel. `memoryMagnitudes` may be null. */
    void processHop (int channelIndex,
                     const float* inputMagnitudes,
                     const float* inputPhases,
                     const float* memoryMagnitudes,
                     const SpectralBlurParams& params) noexcept;

    [[nodiscard]] const float* getBlurMagnitudes (int channelIndex) const noexcept;
    [[nodiscard]] const float* getBlurPhases (int channelIndex) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

private:
    int numBins_ = 0;
    int hopSize_ = constants::hopSize;
    int numChannels_ = 2;
    double sampleRate_ = 44100.0;

    std::vector<std::vector<float>> smearPow_;      // temporal EMA, POWER domain
    std::vector<std::vector<float>> blurMag_;       // sqrt(smearPow), post freq-smear
    std::vector<std::vector<float>> blurPhase_;     // propagated phase (clean)
    std::vector<std::vector<float>> renderPhase_;   // propagated + per-hop scatter
    std::vector<std::vector<float>> blurOmega_;     // held radians-per-hop
    std::vector<std::vector<float>> prevInputPhase_;
    std::vector<std::uint32_t> rng_;
    std::vector<bool> primed_;
    std::vector<bool> hasPrevPhase_;
    std::vector<float> smearScratch_;
    std::vector<float> prefixScratch_;
    LogSmoother smoother_;
};

} // namespace afterimage
