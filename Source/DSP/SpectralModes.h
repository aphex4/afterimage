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

/** Compile-time / developer audition path — not exposed in release UI. */
enum class DebugAudition
{
    Normal = 0,
    RecalledOnly,
    SpectralDelta,
    EraseMask,
    MergeTransfer
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
    float memoryLengthSeconds = constants::memoryLengthDefaultSec;
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

/** Mode-specific retention floor so Forget does not erase moderate Influence. */
[[nodiscard]] float retentionFloorForMode (SpectralMode mode) noexcept;

/**
    Remapped age weight with retention floor:
      historyWeight = floor + (1 - floor) * ageWeightFromForget(...)
*/
[[nodiscard]] float remappedHistoryWeight (SpectralMode mode,
                                           float ageNormalized01,
                                           float forget01) noexcept;

/**
    Perceptual Influence curve: exactly 0 at 0, exactly 1 at 1, stronger mid.
    mapped = 1 - (1 - x)^exponent  (mode-specific exponent).
*/
[[nodiscard]] float mapInfluenceForMode (SpectralMode mode, float influence01) noexcept;

/** Caps how much Transient Preserve can shut off historical contribution. */
inline constexpr float kMaxTransientReduction = 0.65f;

/** Spectral-flux → transientStrength calibration (was 4.0). */
inline constexpr float kTransientFluxCalibration = 3.2f;

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

    Shadow path is isolated and must remain bit-stable unless intentionally changed.

    Erase: persistent per-channel familiarity envelope; contrast-sensitive dB carve.
    Merge: broad envelope transfer + selective historical landmarks (carrier = current).
*/
class SpectralModeProcessor
{
public:
    void prepare (int numBins, double sampleRate = 44100.0, int numChannels = 2);
    void reset();

    /** Clear Erase familiarity envelopes only (history clear / preset load). */
    void clearEraseMemory() noexcept;

    void setMode (SpectralMode mode) noexcept;
    /** Advance mode crossfade without touching magnitudes (e.g. empty history). */
    void tickModeCrossfade (int hopSamples) noexcept;
    [[nodiscard]] SpectralMode getTargetMode() const noexcept { return targetMode_; }
    [[nodiscard]] SpectralMode getPreviousMode() const noexcept { return previousMode_; }
    [[nodiscard]] SpectralMode getLastMode() const noexcept { return lastMode_; }
    /** 0 = fully previous mode, 1 = fully target mode. */
    [[nodiscard]] float getModeAmount() const noexcept { return modeCrossfade_; }

    /** Last smoothed energy scale applied (diagnostic / tests). */
    [[nodiscard]] float getLastEnergyScale (int channelIndex = 0) const noexcept;

    /** Familiarity envelope snapshot (Erase diagnostics / tests). */
    [[nodiscard]] const float* getEraseFamiliarityEnvelope (int channelIndex = 0) const noexcept;
    [[nodiscard]] int getEraseFamiliarityNumBins() const noexcept { return numBins_; }

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

    /**
        Update transient smoother; returns effective mappedInfluence * historyWeight
        after retention floor and max transient reduction.
    */
    [[nodiscard]] float computeMixAmount (SpectralMode mode,
                                          const ModeParams& params,
                                          int channelIndex,
                                          bool updateSmoothers) noexcept;

    /** Bounded, smoothed makeup so quiet history still shapes the mode (Shadow path). */
    [[nodiscard]] const float* normalizeHistoryEnergy (SpectralMode mode,
                                                       const float* historyMagnitudes,
                                                       const float* currentMagnitudes,
                                                       int numBins,
                                                       int channelIndex,
                                                       bool updateSmoothers) noexcept;

    void applyEnergyPolicy (SpectralMode mode,
                            float* magnitudes,
                            int numBins,
                            double energyIn,
                            double energyOut,
                            float mappedInfluence,
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

    /** Shadow-only path — keep algorithmically isolated from Erase/Merge redesign. */
    void applyShadowPath (float* magnitudes,
                          const float* historyMagnitudes,
                          int numBins,
                          const ModeParams& params,
                          int channelIndex,
                          bool updateSmoothers) noexcept;

    void applyErasePath (float* magnitudes,
                         const float* historyMagnitudes,
                         int numBins,
                         const ModeParams& params,
                         int channelIndex,
                         bool updateSmoothers) noexcept;

    void applyMergePath (float* magnitudes,
                         const float* historyMagnitudes,
                         int numBins,
                         const ModeParams& params,
                         int channelIndex,
                         bool updateSmoothers) noexcept;

    void updateEraseFamiliarity (const float* historyMagnitudes,
                                 int numBins,
                                 const ModeParams& params,
                                 int channelIndex) noexcept;

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
    std::vector<float> histNormScratch_;

    // Erase / Merge scratch (preallocated)
    std::vector<float> eraseMaskScratch_;
    std::vector<float> eraseMaskSmoothScratch_;
    std::vector<float> mergeCurEnvScratch_;
    std::vector<float> mergeHistEnvScratch_;
    std::vector<float> mergeLandmarkScratch_;
    std::vector<float> mergeOutScratch_;

    std::vector<float> energyScaleSmoothed_;
    std::vector<float> histMakeupSmoothed_;
    std::vector<float> transientSmoothed_;

    // Per-channel Erase familiarity envelope + mask temporal smooth
    std::vector<std::vector<float>> eraseFamiliarity_;
    std::vector<std::vector<float>> eraseMaskSmoothed_;
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
