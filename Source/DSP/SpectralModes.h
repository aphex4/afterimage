#pragma once

#include "SpectralFrame.h"
#include "SpectralHistoryBuffer.h"
#include "SpectralMemoryProfile.h"
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
    MemoryOnly,          // recalled / profile magnitudes only
    ShadowTailOnly,      // additive tail alone
    EraseRemovedOnly,    // material removed by Erase
    MergeDifferenceOnly  // |out - current|
};

/** Shadow ghost phase strategy (PropagatedGhostPhase OFF until listening proves better). */
enum class ShadowPhaseMode
{
    CurrentPhase = 0,
    PropagatedGhostPhase
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
      t = forget²
      decayCoeff = kMin + t * (kMax - kMin)
*/
[[nodiscard]] float forgetToDecayCoefficient (float forget01) noexcept;

[[nodiscard]] float ageWeightFromForget (float ageNormalized01, float forget01) noexcept;

/** Mode-specific retention floor so Forget does not erase moderate Influence. */
[[nodiscard]] float retentionFloorForMode (SpectralMode mode) noexcept;

[[nodiscard]] float remappedHistoryWeight (SpectralMode mode,
                                           float ageNormalized01,
                                           float forget01) noexcept;

/**
    Perceptual Influence curve: exactly 0 at 0, exactly 1 at 1, stronger mid.
    mapped = 1 - (1 - x)^exponent
*/
[[nodiscard]] float mapInfluenceForMode (SpectralMode mode, float influence01) noexcept;

/** Caps how much Transient Preserve can shut off historical contribution. */
inline constexpr float kMaxTransientReduction = 0.65f;

/** Spectral-flux → transientStrength calibration. */
inline constexpr float kTransientFluxCalibration = 3.2f;

/** Default Shadow phase mode (experiment stays off until proven). */
inline constexpr ShadowPhaseMode kDefaultShadowPhaseMode = ShadowPhaseMode::CurrentPhase;

/** Nonlinear blur radius: round(blur² * maxRadius). blur=0 → 0. */
[[nodiscard]] int blurRadiusFromAmount (float blur01,
                                        int maxRadius = constants::maxBlurRadiusBins) noexcept;

/**
    Separable 1-D box blur via prefix sums — O(numBins).
    radius<=0 copies input→output. prefixScratch holds numBins+1 floats.
*/
void boxBlurMagnitudes (const float* input,
                        float* output,
                        int numBins,
                        int radius,
                        float* prefixScratch) noexcept;

/**
    Soft per-bin contrast limiter: reduce bins that exceed local neighborhood
    by more than maxRelativePeakDb (soft knee).
*/
void applyPerBinContrastLimiter (float* magnitudes,
                                 int numBins,
                                 float maxRelativePeakDb,
                                 float* scratchNeighborhood,
                                 float* prefixScratch) noexcept;

/** Artifact metric: max |mag[k]/mag[k±1]| / mean neighborhood (linear). */
[[nodiscard]] float maxNeighborBinContrast (const float* magnitudes, int numBins) noexcept;

void writeInterleavedFromMagnitudePhase (float* interleavedFftData,
                                         int fftSize,
                                         const float* magnitudes,
                                         const float* phases,
                                         int numBins) noexcept;

/** Reconstruct interleaved FFT using current phase for dry + optional ghost phase for tail. */
void writeInterleavedAdditiveGhost (float* interleavedFftData,
                                    int fftSize,
                                    const float* currentMagnitudes,
                                    const float* currentPhases,
                                    const float* ghostMagnitudes,
                                    const float* ghostPhases,
                                    int numBins) noexcept;

[[nodiscard]] inline bool modeTransformsSpectrum (SpectralMode) noexcept
{
    return true;
}

/**
    Shadow / Erase / Merge spectral transforms on stabilized memory profiles.

    Shadow: multi-age additive spectral tail + diffusion.
    Erase: relative-prominence familiarity map + mask blur.
    Merge: dual-profile log-envelope morph + fine structure.
*/
class SpectralModeProcessor
{
public:
    void prepare (int numBins, double sampleRate = 44100.0, int numChannels = 2);
    void reset();

    /** Clear Erase familiarity envelopes only (history clear / preset load). */
    void clearEraseMemory() noexcept;

    /** Called when Freeze engages — freeze familiarity map (Shadow/Merge use engine profiles). */
    void onFreezeEngaged() noexcept;

    /** Optional scratch profile for Shadow multi-age taps (engine-owned, no alloc). */
    void setTapProfileScratch (SpectralMemoryProfile* scratch) noexcept { tapProfile_ = scratch; }

    void setShadowPhaseMode (ShadowPhaseMode mode) noexcept { shadowPhaseMode_ = mode; }
    [[nodiscard]] ShadowPhaseMode getShadowPhaseMode() const noexcept { return shadowPhaseMode_; }

    /** Last Shadow tail (pre-mix) for PropagatedGhostPhase writeback / tests. */
    [[nodiscard]] const float* getLastShadowTail() const noexcept { return shadowTailScratch_.data(); }
    [[nodiscard]] const float* getGhostPhases (int channelIndex = 0) const noexcept;

    void setMode (SpectralMode mode) noexcept;
    void tickModeCrossfade (int hopSamples) noexcept;
    [[nodiscard]] SpectralMode getTargetMode() const noexcept { return targetMode_; }
    [[nodiscard]] SpectralMode getPreviousMode() const noexcept { return previousMode_; }
    [[nodiscard]] SpectralMode getLastMode() const noexcept { return lastMode_; }
    [[nodiscard]] float getModeAmount() const noexcept { return modeCrossfade_; }

    [[nodiscard]] float getLastEnergyScale (int channelIndex = 0) const noexcept;

    [[nodiscard]] const float* getEraseFamiliarityEnvelope (int channelIndex = 0) const noexcept;
    [[nodiscard]] int getEraseFamiliarityNumBins() const noexcept { return numBins_; }

    /**
        Apply active spectral mode into `frame.magnitudes`.
        `memoryMagnitudes` = effective stabilized profile (Freeze-aware).
        `history` optional — required for Shadow multi-age taps; may be null in unit tests.
    */
    bool process (SpectralMode mode,
                  SpectralFrame& frame,
                  const ModeParams& params,
                  const float* memoryMagnitudes,
                  const SpectralHistoryBuffer* history,
                  int channelIndex,
                  int hopSamples) noexcept;

    /** Direct Shadow magnitude blend + energy compensation (no mode crossfade). */
    void applyShadowMagnitudes (float* magnitudes,
                                const float* historyMagnitudes,
                                int numBins,
                                const ModeParams& params,
                                int channelIndex) noexcept;

    void applyEraseMagnitudes (float* magnitudes,
                               const float* historyMagnitudes,
                               int numBins,
                               const ModeParams& params,
                               int channelIndex) noexcept;

    void applyMergeMagnitudes (float* magnitudes,
                               const float* historyMagnitudes,
                               int numBins,
                               const ModeParams& params,
                               int channelIndex) noexcept;

    /**
        Build multi-age Shadow tail into dest (for tests).
        Uses history when available; otherwise treats historyMagnitudes as single tap.
    */
    void buildShadowTail (float* dest,
                          int numBins,
                          const float* memoryMagnitudes,
                          const SpectralHistoryBuffer* history,
                          const ModeParams& params,
                          int channelIndex,
                          bool updateSmoothers) noexcept;

private:
    void noteModeChange (SpectralMode mode) noexcept;
    void advanceModeCrossfade (int hopSamples) noexcept;
    void ensureChannelState (int channelIndex) noexcept;

    void diffuseMagnitudes (const float* input, float* output, int numBins, float blur01) noexcept;

    [[nodiscard]] float computeMixAmount (SpectralMode mode,
                                          const ModeParams& params,
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
                              const float* memoryMagnitudes,
                              const SpectralHistoryBuffer* history,
                              int numBins,
                              const ModeParams& params,
                              int channelIndex,
                              bool updateSmoothers) noexcept;

    void applyShadowPath (float* magnitudes,
                          const float* memoryMagnitudes,
                          const SpectralHistoryBuffer* history,
                          int numBins,
                          const ModeParams& params,
                          int channelIndex,
                          bool updateSmoothers) noexcept;

    void applyErasePath (float* magnitudes,
                         const float* memoryMagnitudes,
                         int numBins,
                         const ModeParams& params,
                         int channelIndex,
                         bool updateSmoothers) noexcept;

    void applyMergePath (float* magnitudes,
                         const float* memoryMagnitudes,
                         int numBins,
                         const ModeParams& params,
                         int channelIndex,
                         bool updateSmoothers) noexcept;

    void updateEraseFamiliarity (const float* memoryMagnitudes,
                                 int numBins,
                                 const ModeParams& params,
                                 int channelIndex) noexcept;

    void advanceGhostPhases (int numBins, int channelIndex) noexcept;

    int numBins_ = 0;
    int numChannels_ = 2;
    double sampleRate_ = 44100.0;

    SpectralMode targetMode_ = SpectralMode::Shadow;
    SpectralMode previousMode_ = SpectralMode::Shadow;
    SpectralMode lastMode_ = SpectralMode::Shadow;
    float modeCrossfade_ = 1.0f;

    ShadowPhaseMode shadowPhaseMode_ = kDefaultShadowPhaseMode;
    SpectralMemoryProfile* tapProfile_ = nullptr;

    std::vector<float> blurScratch_;
    std::vector<float> prefixScratch_;
    std::vector<float> identityScratch_;
    std::vector<float> crossfadeScratch_;
    std::vector<float> shadowTailScratch_;
    std::vector<float> diffuseScratchA_;
    std::vector<float> diffuseScratchB_;
    std::vector<float> limiterScratch_;

    // Erase / Merge scratch
    std::vector<float> eraseMaskScratch_;
    std::vector<float> eraseMaskSmoothScratch_;
    std::vector<float> broadEnvScratch_;
    std::vector<float> mergeCurEnvScratch_;
    std::vector<float> mergeHistEnvScratch_;
    std::vector<float> mergeCurProfileScratch_;
    std::vector<float> mergeOutScratch_;

    std::vector<float> energyScaleSmoothed_;
    std::vector<float> transientSmoothed_;

    // Per-channel Erase familiarity + mask temporal smooth + ghost phase
    std::vector<std::vector<float>> eraseFamiliarity_;
    std::vector<std::vector<float>> eraseMaskSmoothed_;
    std::vector<std::vector<float>> ghostPhases_;
    std::vector<bool> eraseFamiliarityFrozen_;

    // Short current-profile EMA for Merge (40–100 ms)
    std::vector<std::vector<float>> mergeCurrentProfile_;
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
