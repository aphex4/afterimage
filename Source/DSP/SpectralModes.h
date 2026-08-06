#pragma once

#include "SpectralFrame.h"
#include "../Utilities/Constants.h"

#include <vector>

namespace afterimage
{

enum class SpectralMode
{
    Shadow = 0,
    Erase,
    Merge
};

struct ModeParams
{
    float influence = 0.5f;
    float recallPosition = 0.45f;
    float forget = 0.35f;
    float blur = 0.15f;
    float transientPreserve = 0.5f;
    float randomRecall = 0.0f;
    float transientStrength = 0.0f;
    float recallAge01 = 0.0f; // same age used for L/R history lookup
    bool  freeze = false;
};

//==============================================================================
// Free helpers (unit-testable, RT-safe, no allocation)
//==============================================================================

/**
    Nonlinear Forget → exponential decay coefficient.

    Mapping (documented):
      forget ∈ [0, 1]
      t = forget²                          // soft near 0, steep near 1
      decayCoeff = kMin + t * (kMax - kMin)
      kMin = 0.05, kMax = 8.0

    ageWeight = exp(-ageNormalized * decayCoeff)
    Higher Forget → larger coeff → stronger attenuation of older recalls.
*/
[[nodiscard]] float forgetToDecayCoefficient (float forget01) noexcept;

[[nodiscard]] float ageWeightFromForget (float ageNormalized01, float forget01) noexcept;

/** Nonlinear blur radius: round(blur² * maxRadius). blur=0 → 0 (exact identity). */
[[nodiscard]] int blurRadiusFromAmount (float blur01,
                                        int maxRadius = constants::maxBlurRadiusBins) noexcept;

/**
    Separable 1-D box blur via prefix sums — O(numBins) per call.
    Edge bins use a clamped window (fewer taps). radius<=0 copies input→output.
    prefixScratch must hold numBins+1 floats (preallocated).
*/
void boxBlurMagnitudes (const float* input,
                        float* output,
                        int numBins,
                        int radius,
                        float* prefixScratch) noexcept;

/**
    Reconstruct interleaved real-FFT bins from magnitude + phase (cos/sin).
    DC and Nyquist imag forced to 0 (JUCE real-only layout).
*/
void writeInterleavedFromMagnitudePhase (float* interleavedFftData,
                                         int fftSize,
                                         const float* magnitudes,
                                         const float* phases,
                                         int numBins) noexcept;

/** All three modes transform magnitudes when Influence > 0 and history exists. */
[[nodiscard]] inline bool modeTransformsSpectrum (SpectralMode) noexcept
{
    return true;
}

/** @deprecated Prefer modeTransformsSpectrum — kept for older call sites. */
[[nodiscard]] inline bool modeAppliesShadow (SpectralMode mode) noexcept
{
    return mode == SpectralMode::Shadow;
}

/**
    Shadow / Erase / Merge spectral transforms.

    Phase 5: all three modes modify magnitudes. A short any-mode crossfade
    (constants::modeCrossfadeSec) dual-applies previous→target so switches stay click-free.
*/
class SpectralModeProcessor
{
public:
    void prepare (int numBins, double sampleRate = 44100.0, int numChannels = 2);
    void reset();

    void setMode (SpectralMode mode) noexcept;
    /** Advance mode crossfade without touching magnitudes (e.g. empty history). */
    void tickModeCrossfade (int hopSamples) noexcept;
    [[nodiscard]] SpectralMode getTargetMode() const noexcept { return targetMode_; }
    [[nodiscard]] SpectralMode getPreviousMode() const noexcept { return previousMode_; }
    [[nodiscard]] SpectralMode getLastMode() const noexcept { return lastMode_; }
    /** 0 = fully previous mode, 1 = fully target mode. */
    [[nodiscard]] float getModeAmount() const noexcept { return modeCrossfade_; }

    /**
        Apply the active spectral mode into `frame.magnitudes` in-place.
        Pass unblurred history; this blurs into scratch when blur > 0.

        Returns true if magnitudes were changed (caller should write FFT back).
        Returns false for exact identity — leave interleaved FFT untouched.
    */
    bool process (SpectralMode mode,
                  SpectralFrame& frame,
                  const ModeParams& params,
                  const float* historyMagnitudes,
                  int channelIndex,
                  int hopSamples) noexcept;

    /** Direct Shadow magnitude blend + energy compensation (no mode crossfade). */
    void applyShadowMagnitudes (float* magnitudes,
                                const float* historyMagnitudes,
                                int numBins,
                                const ModeParams& params,
                                int channelIndex) noexcept;

    /** Direct Erase suppression + energy compensation (no mode crossfade). */
    void applyEraseMagnitudes (float* magnitudes,
                               const float* historyMagnitudes,
                               int numBins,
                               const ModeParams& params,
                               int channelIndex) noexcept;

    /** Direct Merge morph + energy compensation (no mode crossfade). */
    void applyMergeMagnitudes (float* magnitudes,
                               const float* historyMagnitudes,
                               int numBins,
                               const ModeParams& params,
                               int channelIndex) noexcept;

private:
    void noteModeChange (SpectralMode mode) noexcept;
    void advanceModeCrossfade (int hopSamples) noexcept;
    void ensureChannelState (int channelIndex) noexcept;

    [[nodiscard]] const float* prepareBlurredHistory (const float* historyMagnitudes,
                                                      int numBins,
                                                      float blur01) noexcept;

    /** Update transient smoother; returns effectiveInfluence * historyWeight. */
    [[nodiscard]] float computeMixAmount (const ModeParams& params,
                                          int channelIndex,
                                          bool updateSmoothers) noexcept;

    void applyEnergyCompensation (float* magnitudes,
                                  int numBins,
                                  double energyIn,
                                  double energyOut,
                                  int channelIndex,
                                  bool updateSmoothers) noexcept;

    void sanitizeMagnitudes (float* magnitudes, int numBins) noexcept;

    void applyModeMagnitudes (SpectralMode mode,
                              float* magnitudes,
                              const float* historyMagnitudes,
                              int numBins,
                              const ModeParams& params,
                              int channelIndex,
                              bool updateSmoothers) noexcept;

    int numBins_ = 0;
    int numChannels_ = 2;
    double sampleRate_ = 44100.0;

    SpectralMode targetMode_ = SpectralMode::Shadow;
    SpectralMode previousMode_ = SpectralMode::Shadow;
    SpectralMode lastMode_ = SpectralMode::Shadow;
    float modeCrossfade_ = 1.0f; // 1 = fully on targetMode_

    std::vector<float> blurScratch_;
    std::vector<float> prefixScratch_;
    std::vector<float> identityScratch_;
    std::vector<float> crossfadeScratch_;

    std::vector<float> energyScaleSmoothed_;
    std::vector<float> transientSmoothed_;
};

[[nodiscard]] inline const char* spectralModeName (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow: return "SHADOW";
        case SpectralMode::Erase:  return "ERASE";
        case SpectralMode::Merge:  return "MERGE";
    }
    return "SHADOW";
}

} // namespace afterimage
