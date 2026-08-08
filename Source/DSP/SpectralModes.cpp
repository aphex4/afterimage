#include "SpectralModes.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace afterimage
{

namespace
{
constexpr float kForgetDecayMin = 0.05f;
constexpr float kForgetDecayMax = 8.0f;
constexpr float kEnergyEpsilon = 1.0e-12f;
constexpr float kInfluenceEpsilon = 1.0e-5f;
constexpr float kModeAmountEpsilon = 1.0e-4f;
constexpr float kTransientSmoothCoeff = 0.35f;
constexpr float kSilenceEnergyThresh = 1.0e-10f;

// Shadow multi-age tail (removed — SpectralTail)
constexpr float kContrastLimitDb = 30.0f;

// Absolute ceiling (~+12 dBFS vs unit-sine FFT peak ≈ N/2)
constexpr float kAbsoluteCeilingAttackMs = 5.0f;
constexpr float kAbsoluteCeilingReleaseMs = 2000.0f;
constexpr float kAbsoluteCeilingRampMs = 500.0f;

// Erase familiarity
constexpr float kEraseFamiliarityKnee = 0.12f;
constexpr float kEraseContrastExp = 1.05f;
constexpr float kEraseMaskTemporalCoeff = 0.68f;
constexpr float kEraseSoftFloorLin = 0.03f;
constexpr int   kEraseMaskBaseRadius = 1;
constexpr int   kEraseBroadEnvRadius = 14;

// Merge
constexpr float kMergeDbEpsilon = 1.0e-8f;
constexpr float kMergeCurrentProfileMs = 70.0f;
constexpr int   kMergeEnvBaseRadius = 12;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
constexpr DebugAudition kDebugAudition = static_cast<DebugAudition> (AFTERIMAGE_DEBUG_AUDITION);
#else
constexpr DebugAudition kDebugAudition = DebugAudition::Normal;
#endif

[[nodiscard]] float influenceExponentForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow: return 1.30f;
        case SpectralMode::Erase:  return 1.50f;
        case SpectralMode::Merge:  return 1.20f;
    }
    return 1.30f;
}

[[nodiscard]] float absoluteCeilingThreshold() noexcept
{
    // Unit-amplitude sine ≈ fftSize/2 in JUCE unnormalized real FFT magnitudes.
    return constants::dbToGain (12.0f) * 0.5f * static_cast<float> (constants::fftSize);
}

[[nodiscard]] double sumSq (const float* m, int n) noexcept
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
        e += static_cast<double> (m[i]) * static_cast<double> (m[i]);
    return e;
}
} // namespace

//==============================================================================
float forgetToDecayCoefficient (float forget01) noexcept
{
    const float f = juce::jlimit (0.0f, 1.0f, forget01);
    const float t = f * f;
    return kForgetDecayMin + t * (kForgetDecayMax - kForgetDecayMin);
}

float ageWeightFromForget (float ageNormalized01, float forget01) noexcept
{
    const float age = juce::jlimit (0.0f, 1.0f, ageNormalized01);
    const float coeff = forgetToDecayCoefficient (forget01);
    const float w = std::exp (-age * coeff);
    return juce::jlimit (0.0f, 1.0f, w);
}

float retentionFloorForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow: return 0.22f;
        case SpectralMode::Erase:  return 0.15f;
        case SpectralMode::Merge:  return 0.25f;
    }
    return 0.22f;
}

float remappedHistoryWeight (SpectralMode mode,
                             float ageNormalized01,
                             float forget01) noexcept
{
    const float floor = retentionFloorForMode (mode);
    const float ageW = ageWeightFromForget (ageNormalized01, forget01);
    return juce::jlimit (0.0f, 1.0f, floor + (1.0f - floor) * ageW);
}

float mapInfluenceForMode (SpectralMode mode, float influence01) noexcept
{
    const float x = juce::jlimit (0.0f, 1.0f, influence01);
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;

    const float expn = influenceExponentForMode (mode);
    const float mapped = 1.0f - std::pow (1.0f - x, expn);
    return juce::jlimit (0.0f, 1.0f, mapped);
}

int blurRadiusFromAmount (float blur01, int maxRadius) noexcept
{
    const float b = juce::jlimit (0.0f, 1.0f, blur01);
    const int r = static_cast<int> (std::lround (static_cast<double> (b * b)
                                                 * static_cast<double> (std::max (0, maxRadius))));
    return juce::jlimit (0, std::max (0, maxRadius), r);
}

void boxBlurMagnitudes (const float* input,
                        float* output,
                        int numBins,
                        int radius,
                        float* prefixScratch) noexcept
{
    if (input == nullptr || output == nullptr || numBins <= 0)
        return;

    if (radius <= 0)
    {
        if (output != input)
            std::copy (input, input + numBins, output);
        return;
    }

    if (prefixScratch == nullptr)
    {
        if (output != input)
            std::copy (input, input + numBins, output);
        return;
    }

    prefixScratch[0] = 0.0f;
    for (int i = 0; i < numBins; ++i)
        prefixScratch[i + 1] = prefixScratch[i] + input[i];

    for (int i = 0; i < numBins; ++i)
    {
        const int lo = std::max (0, i - radius);
        const int hi = std::min (numBins - 1, i + radius);
        const float sum = prefixScratch[hi + 1] - prefixScratch[lo];
        const int count = hi - lo + 1;
        output[i] = sum / static_cast<float> (std::max (1, count));
    }
}

void applyPerBinContrastLimiter (float* magnitudes,
                                 int numBins,
                                 float maxRelativePeakDb,
                                 float* scratchNeighborhood,
                                 float* prefixScratch) noexcept
{
    if (magnitudes == nullptr || scratchNeighborhood == nullptr || numBins <= 0)
        return;

    constexpr int kNeighborRadius = 2;
    boxBlurMagnitudes (magnitudes, scratchNeighborhood, numBins, kNeighborRadius, prefixScratch);

    const float maxRatio = constants::dbToGain (std::max (6.0f, maxRelativePeakDb));
    const float softKnee = 0.35f;

    for (int i = 0; i < numBins; ++i)
    {
        const float local = scratchNeighborhood[i] + 1.0e-8f;
        const float mag = magnitudes[i];
        const float ratio = mag / local;
        if (ratio <= maxRatio)
            continue;

        const float target = local * maxRatio;
        // Soft knee: blend toward the limit without flattening harmonics.
        magnitudes[i] = mag + (target - mag) * softKnee;
        if (! std::isfinite (magnitudes[i]))
            magnitudes[i] = 0.0f;
        magnitudes[i] = std::max (0.0f, magnitudes[i]);
    }
}

float maxNeighborBinContrast (const float* magnitudes, int numBins) noexcept
{
    if (magnitudes == nullptr || numBins < 2)
        return 0.0f;

    float maxC = 0.0f;
    for (int i = 1; i < numBins; ++i)
    {
        const float a = magnitudes[i - 1] + 1.0e-8f;
        const float b = magnitudes[i] + 1.0e-8f;
        const float c = std::max (a, b) / std::min (a, b);
        maxC = std::max (maxC, c);
    }
    return maxC;
}

void writeInterleavedFromMagnitudePhase (float* interleavedFftData,
                                         int fftSize,
                                         const float* magnitudes,
                                         const float* phases,
                                         int numBins) noexcept
{
    if (interleavedFftData == nullptr || magnitudes == nullptr || phases == nullptr)
        return;

    const int nyquist = fftSize / 2;
    const int n = std::min (numBins, nyquist + 1);

    for (int k = 0; k < n; ++k)
    {
        const float mag = magnitudes[k];
        const float phase = phases[k];
        const float re = mag * std::cos (phase);
        const float im = mag * std::sin (phase);

        interleavedFftData[2 * k] = re;

        if (k == 0 || k == nyquist)
            interleavedFftData[2 * k + 1] = 0.0f;
        else
            interleavedFftData[2 * k + 1] = im;
    }
}

void writeInterleavedWithTail (float* interleavedFftData,
                               int fftSize,
                               const float* dryMagnitudes,
                               const float* dryPhases,
                               const float* tailMagnitudes,
                               const float* tailPhases,
                               float tailGain,
                               int numBins) noexcept
{
    if (interleavedFftData == nullptr || dryMagnitudes == nullptr || dryPhases == nullptr)
        return;

    const int nyquist = fftSize / 2;
    const int n = std::min (numBins, nyquist + 1);
    const bool hasTail = tailMagnitudes != nullptr && tailPhases != nullptr
                         && std::abs (tailGain) > 1.0e-12f;

    for (int k = 0; k < n; ++k)
    {
        float re = dryMagnitudes[k] * std::cos (dryPhases[k]);
        float im = dryMagnitudes[k] * std::sin (dryPhases[k]);

        if (hasTail)
        {
            re += tailGain * tailMagnitudes[k] * std::cos (tailPhases[k]);
            im += tailGain * tailMagnitudes[k] * std::sin (tailPhases[k]);
        }

        interleavedFftData[2 * k] = re;
        if (k == 0 || k == nyquist)
            interleavedFftData[2 * k + 1] = 0.0f;
        else
            interleavedFftData[2 * k + 1] = im;
    }
}

//==============================================================================
void SpectralModeProcessor::prepare (int numBins, double sampleRate, int numChannels)
{
    numBins_ = std::max (0, numBins);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    numChannels_ = std::max (1, numChannels);

    blurScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    prefixScratch_.assign (static_cast<std::size_t> (numBins_ + 1), 0.0f);
    identityScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    crossfadeScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    shadowInjectScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    shadowPhaseScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    diffuseScratchA_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    diffuseScratchB_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    limiterScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);

    eraseMaskScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    eraseMaskSmoothScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    broadEnvScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeCurEnvScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeHistEnvScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeCurProfileScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeOutScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);

    energyScaleSmoothed_.assign (static_cast<std::size_t> (numChannels_), 1.0f);
    runningPeak_.assign (static_cast<std::size_t> (numChannels_), 0.0f);
    ceilingScale_.assign (static_cast<std::size_t> (numChannels_), 1.0f);
    transientSmoothed_.assign (static_cast<std::size_t> (numChannels_), 0.0f);

    eraseFamiliarity_.assign (static_cast<std::size_t> (numChannels_),
                              std::vector<float> (static_cast<std::size_t> (numBins_), 0.0f));
    eraseMaskSmoothed_.assign (static_cast<std::size_t> (numChannels_),
                               std::vector<float> (static_cast<std::size_t> (numBins_), 0.0f));
    eraseFamiliarityFrozen_.assign (static_cast<std::size_t> (numChannels_), false);

    mergeCurrentProfile_.assign (static_cast<std::size_t> (numChannels_),
                                 std::vector<float> (static_cast<std::size_t> (numBins_), 0.0f));

    spectralTail_.prepare (numBins_, sampleRate_, constants::hopSize, numChannels_);

    reset();
}

void SpectralModeProcessor::reset()
{
    lastMode_ = SpectralMode::Shadow;
    targetMode_ = SpectralMode::Shadow;
    previousMode_ = SpectralMode::Shadow;
    modeCrossfade_ = 1.0f;
    shadowComplexWrite_ = false;
    lastShadowTailGain_ = 0.0f;
    lastShadowDiffusion_ = 0.0f;

    std::fill (blurScratch_.begin(), blurScratch_.end(), 0.0f);
    std::fill (prefixScratch_.begin(), prefixScratch_.end(), 0.0f);
    std::fill (identityScratch_.begin(), identityScratch_.end(), 0.0f);
    std::fill (crossfadeScratch_.begin(), crossfadeScratch_.end(), 0.0f);
    std::fill (shadowInjectScratch_.begin(), shadowInjectScratch_.end(), 0.0f);
    std::fill (shadowPhaseScratch_.begin(), shadowPhaseScratch_.end(), 0.0f);
    std::fill (diffuseScratchA_.begin(), diffuseScratchA_.end(), 0.0f);
    std::fill (diffuseScratchB_.begin(), diffuseScratchB_.end(), 0.0f);
    std::fill (limiterScratch_.begin(), limiterScratch_.end(), 0.0f);
    std::fill (eraseMaskScratch_.begin(), eraseMaskScratch_.end(), 0.0f);
    std::fill (eraseMaskSmoothScratch_.begin(), eraseMaskSmoothScratch_.end(), 0.0f);
    std::fill (broadEnvScratch_.begin(), broadEnvScratch_.end(), 0.0f);
    std::fill (mergeCurEnvScratch_.begin(), mergeCurEnvScratch_.end(), 0.0f);
    std::fill (mergeHistEnvScratch_.begin(), mergeHistEnvScratch_.end(), 0.0f);
    std::fill (mergeCurProfileScratch_.begin(), mergeCurProfileScratch_.end(), 0.0f);
    std::fill (mergeOutScratch_.begin(), mergeOutScratch_.end(), 0.0f);
    std::fill (energyScaleSmoothed_.begin(), energyScaleSmoothed_.end(), 1.0f);
    std::fill (runningPeak_.begin(), runningPeak_.end(), 0.0f);
    std::fill (ceilingScale_.begin(), ceilingScale_.end(), 1.0f);
    std::fill (transientSmoothed_.begin(), transientSmoothed_.end(), 0.0f);
    std::fill (eraseFamiliarityFrozen_.begin(), eraseFamiliarityFrozen_.end(), false);

    for (auto& m : mergeCurrentProfile_)
        std::fill (m.begin(), m.end(), 0.0f);

    spectralTail_.reset();
    clearEraseMemory();
}

void SpectralModeProcessor::clearEraseMemory() noexcept
{
    for (auto& env : eraseFamiliarity_)
        std::fill (env.begin(), env.end(), 0.0f);
    for (auto& m : eraseMaskSmoothed_)
        std::fill (m.begin(), m.end(), 0.0f);
    std::fill (eraseFamiliarityFrozen_.begin(), eraseFamiliarityFrozen_.end(), false);
}

void SpectralModeProcessor::onFreezeEngaged() noexcept
{
    std::fill (eraseFamiliarityFrozen_.begin(), eraseFamiliarityFrozen_.end(), true);
}

const float* SpectralModeProcessor::getShadowTailMagnitudes (int channelIndex) const noexcept
{
    return spectralTail_.getTailMagnitudes (channelIndex);
}

const float* SpectralModeProcessor::getShadowTailPhases (int channelIndex) const noexcept
{
    return spectralTail_.getTailPhases (channelIndex);
}

void SpectralModeProcessor::setMode (SpectralMode mode) noexcept
{
    noteModeChange (mode);
}

void SpectralModeProcessor::noteModeChange (SpectralMode mode) noexcept
{
    if (mode == targetMode_)
        return;

    previousMode_ = targetMode_;
    targetMode_ = mode;
    modeCrossfade_ = 0.0f;
}

void SpectralModeProcessor::ensureChannelState (int channelIndex) noexcept
{
    if (channelIndex < 0)
        return;

    const auto need = static_cast<std::size_t> (channelIndex + 1);
    jassert (need <= ceilingScale_.size());
    jassert (need <= runningPeak_.size());
    jassert (need <= transientSmoothed_.size());
    jassert (need <= eraseFamiliarity_.size());
    juce::ignoreUnused (need);
}

float SpectralModeProcessor::getLastEnergyScale (int channelIndex) const noexcept
{
    if (ceilingScale_.empty())
        return 1.0f;
    const auto ch = static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (ceilingScale_.size()) - 1, channelIndex));
    return ceilingScale_[ch];
}

const float* SpectralModeProcessor::getEraseFamiliarityEnvelope (int channelIndex) const noexcept
{
    if (eraseFamiliarity_.empty() || numBins_ <= 0)
        return nullptr;
    const auto ch = static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (eraseFamiliarity_.size()) - 1, channelIndex));
    return eraseFamiliarity_[ch].data();
}

void SpectralModeProcessor::advanceModeCrossfade (int hopSamples) noexcept
{
    if (modeCrossfade_ >= 1.0f - kModeAmountEpsilon)
    {
        modeCrossfade_ = 1.0f;
        previousMode_ = targetMode_;
        return;
    }

    const double fadeSec = static_cast<double> (constants::modeCrossfadeSec);
    const double hopsPerSec = sampleRate_ / static_cast<double> (std::max (1, hopSamples));
    const float alpha = static_cast<float> (1.0 / std::max (1.0, fadeSec * hopsPerSec));
    modeCrossfade_ = std::min (1.0f, modeCrossfade_ + alpha);

    if (modeCrossfade_ >= 1.0f - kModeAmountEpsilon)
    {
        modeCrossfade_ = 1.0f;
        previousMode_ = targetMode_;
    }
}

void SpectralModeProcessor::tickModeCrossfade (int hopSamples) noexcept
{
    advanceModeCrossfade (hopSamples);
}

void SpectralModeProcessor::diffuseMagnitudes (const float* input,
                                               float* output,
                                               int numBins,
                                               float blur01) noexcept
{
    if (input == nullptr || output == nullptr || numBins <= 0)
        return;

    const float b = juce::jlimit (0.0f, 1.0f, blur01);
    // Low blur: 0–1 light diffusion passes; high: up to ~6
    const int passes = static_cast<int> (std::lround (b * b * 6.0f));
    if (passes <= 0)
    {
        if (output != input)
            std::copy (input, input + numBins, output);
        return;
    }

    const float* src = input;
    float* dst = diffuseScratchA_.data();
    float* alt = diffuseScratchB_.data();

    for (int p = 0; p < passes; ++p)
    {
        // 3-tap: 0.2 | 0.6 | 0.2
        dst[0] = 0.8f * src[0] + 0.2f * src[1];
        for (int k = 1; k < numBins - 1; ++k)
            dst[k] = 0.2f * src[k - 1] + 0.6f * src[k] + 0.2f * src[k + 1];
        dst[numBins - 1] = 0.8f * src[numBins - 1] + 0.2f * src[numBins - 2];

        src = dst;
        std::swap (dst, alt);
    }

    // Preserve RMS vs original input
    const double eIn = sumSq (input, numBins);
    const double eOut = sumSq (src, numBins);
    float scale = 1.0f;
    if (eIn > static_cast<double> (kSilenceEnergyThresh)
        && eOut > static_cast<double> (kEnergyEpsilon))
    {
        scale = juce::jlimit (0.5f, 2.0f, static_cast<float> (std::sqrt (eIn / eOut)));
    }

    for (int k = 0; k < numBins; ++k)
        output[k] = src[k] * scale;
}

float SpectralModeProcessor::computeMixAmount (SpectralMode mode,
                                               const ModeParams& params,
                                               int channelIndex,
                                               bool updateSmoothers) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    float trSmooth = transientSmoothed_[ch];
    if (updateSmoothers)
    {
        trSmooth += (params.transientStrength - trSmooth) * kTransientSmoothCoeff;
        trSmooth = juce::jlimit (0.0f, 1.0f, trSmooth);
        transientSmoothed_[ch] = trSmooth;
    }

    const float preserve = juce::jlimit (0.0f, 1.0f, params.transientPreserve);
    const float mapped = mapInfluenceForMode (mode, params.influence);
    const float reduction = trSmooth * preserve * kMaxTransientReduction;
    const float duck = 1.0f - juce::jlimit (0.0f, 1.0f, reduction);
    return juce::jlimit (0.0f, 1.0f, mapped * duck);
}

void SpectralModeProcessor::applyAbsoluteCeiling (float* magnitudes,
                                                  int numBins,
                                                  int channelIndex) noexcept
{
    if (magnitudes == nullptr || numBins <= 0)
        return;

    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    float framePeak = 0.0f;
    for (int i = 0; i < numBins; ++i)
        framePeak = std::max (framePeak, magnitudes[i]);

    const float hopSec = static_cast<float> (constants::hopSize)
                         / static_cast<float> (std::max (1.0e-6, sampleRate_));
    const float attackCoeff = 1.0f - std::exp (-hopSec / (kAbsoluteCeilingAttackMs * 0.001f));
    const float releaseCoeff = 1.0f - std::exp (-hopSec / (kAbsoluteCeilingReleaseMs * 0.001f));
    const float rampCoeff = 1.0f - std::exp (-hopSec / (kAbsoluteCeilingRampMs * 0.001f));

    float& peak = runningPeak_[ch];
    if (framePeak > peak)
        peak += (framePeak - peak) * attackCoeff;
    else
        peak += (framePeak - peak) * releaseCoeff;

    const float threshold = absoluteCeilingThreshold();
    float targetScale = 1.0f;
    if (peak > threshold && peak > 1.0e-12f)
        targetScale = threshold / peak;

    float& scale = ceilingScale_[ch];
    scale += (targetScale - scale) * rampCoeff;
    scale = juce::jlimit (0.05f, 1.0f, scale);
    energyScaleSmoothed_[ch] = scale; // keep diagnostic alias populated

    if (scale < 1.0f - 1.0e-6f)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] *= scale;
    }
}

void SpectralModeProcessor::sanitizeMagnitudes (float* magnitudes, int numBins) noexcept
{
    for (int i = 0; i < numBins; ++i)
    {
        if (! std::isfinite (magnitudes[i]))
            magnitudes[i] = 0.0f;
        else
            magnitudes[i] = std::max (0.0f, magnitudes[i]);
    }
}

//==============================================================================
void SpectralModeProcessor::applyShadowPath (float* magnitudes,
                                             const float* phases,
                                             const float* memoryMagnitudes,
                                             const SpectralHistoryBuffer* history,
                                             int numBins,
                                             const ModeParams& params,
                                             int channelIndex,
                                             bool updateSmoothers,
                                             bool leaveDryForComplexWrite) noexcept
{
    if (magnitudes == nullptr || numBins <= 0)
        return;

    juce::ignoreUnused (memoryMagnitudes);

    ensureChannelState (channelIndex);

    // Injection: live magnitudes, crossfaded toward recalled history as Recall rises.
    // Without a history buffer (unit tests), memoryMagnitudes stand in as the inject source.
    const float recallAmt = juce::jlimit (0.0f, 1.0f, params.recallPosition);
    const float* injectMags = magnitudes;

    if (history != nullptr && recallAmt > 1.0e-4f
        && static_cast<int> (shadowInjectScratch_.size()) >= numBins)
    {
        history->getInterpolatedMagnitudes (params.recallAge01,
                                            shadowInjectScratch_.data(),
                                            numBins);
        for (int i = 0; i < numBins; ++i)
        {
            shadowInjectScratch_[static_cast<std::size_t> (i)] =
                magnitudes[i] * (1.0f - recallAmt)
                + shadowInjectScratch_[static_cast<std::size_t> (i)] * recallAmt;
        }
        injectMags = shadowInjectScratch_.data();
    }
    else if (history == nullptr && memoryMagnitudes != nullptr)
    {
        injectMags = memoryMagnitudes;
    }

    const float* injectPhases = phases;
    if (injectPhases == nullptr)
    {
        std::fill (shadowPhaseScratch_.begin(),
                   shadowPhaseScratch_.begin() + numBins,
                   0.0f);
        injectPhases = shadowPhaseScratch_.data();
    }

    const float forget = juce::jlimit (0.0f, 1.0f, params.forget);
    const float memSec = juce::jlimit (constants::memoryLengthMinSec,
                                       constants::memoryLengthMaxSec,
                                       params.memoryLengthSeconds);
    const float blur = juce::jlimit (0.0f, 1.0f, params.blur);

    SpectralTailParams tp;
    tp.rt60Seconds = juce::jlimit (0.1f, 30.0f,
                                   memSec * std::pow (4.0f, 1.0f - 2.0f * forget));
    tp.hfDampRatio = 0.35f;
    {
        const float x = juce::jlimit (0.0f, 1.0f, params.influence);
        float inject = (x <= 0.0f) ? 0.0f
                     : (x >= 1.0f) ? 1.0f
                                   : (1.0f - std::pow (1.0f - x, 1.5f));
        const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));
        const float preserve = juce::jlimit (0.0f, 1.0f, params.transientPreserve);
        const float reduction = transientSmoothed_[ch] * preserve * kMaxTransientReduction;
        // Keep transient smoother warm even when Influence uses its own inject curve.
        (void) computeMixAmount (SpectralMode::Shadow, params, channelIndex, updateSmoothers);
        inject *= (1.0f - juce::jlimit (0.0f, 1.0f, reduction));
        tp.injectGain = inject;
    }
    tp.diffusion = blur;
    tp.shimmerCents = blur * 12.0f;
    tp.freeze = params.freeze;

    if (updateSmoothers)
        spectralTail_.processHop (channelIndex, injectMags, injectPhases, tp);

    const float mixAmount = mapInfluenceForMode (SpectralMode::Shadow, params.influence);
    const float olaComp = 1.0f + tp.diffusion * (constants::incoherentOlaCompensation - 1.0f);
    lastShadowDiffusion_ = tp.diffusion;
    lastShadowTailGain_ = juce::jlimit (0.0f, 8.0f, mixAmount * olaComp);
    shadowComplexWrite_ = leaveDryForComplexWrite;

    const float* tailMag = spectralTail_.getTailMagnitudes (channelIndex);
    if (tailMag == nullptr)
        return;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::MemoryOnly
        || kDebugAudition == DebugAudition::ShadowTailOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = tailMag[i] * (kDebugAudition == DebugAudition::ShadowTailOnly
                                              ? lastShadowTailGain_ : 1.0f);
        sanitizeMagnitudes (magnitudes, numBins);
        shadowComplexWrite_ = false;
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    if (leaveDryForComplexWrite)
    {
        // Dry magnitudes untouched — engine composites via writeInterleavedWithTail.
        return;
    }

    // Unit-test / mode-crossfade path: fold tail into magnitudes (shared phase).
    for (int i = 0; i < numBins; ++i)
        magnitudes[i] += lastShadowTailGain_ * tailMag[i];

    applyAbsoluteCeiling (magnitudes, numBins, channelIndex);
    applyPerBinContrastLimiter (magnitudes, numBins, kContrastLimitDb,
                                limiterScratch_.data(), prefixScratch_.data());
    sanitizeMagnitudes (magnitudes, numBins);
}

//==============================================================================
void SpectralModeProcessor::updateEraseFamiliarity (const float* memoryMagnitudes,
                                                    int numBins,
                                                    const ModeParams& params,
                                                    int channelIndex) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    // Freeze holds the familiarity map (stencil). Engine also calls onFreezeEngaged().
    if (params.freeze)
        return;

    if (ch < eraseFamiliarityFrozen_.size())
        eraseFamiliarityFrozen_[ch] = false;

    auto& env = eraseFamiliarity_[ch];
    if (static_cast<int> (env.size()) < numBins || memoryMagnitudes == nullptr)
        return;

    // Broad envelope for relative prominence (repetition ≠ loudness)
    boxBlurMagnitudes (memoryMagnitudes, broadEnvScratch_.data(), numBins,
                       kEraseBroadEnvRadius, prefixScratch_.data());

    const float memSec = juce::jlimit (constants::memoryLengthMinSec,
                                       constants::memoryLengthMaxSec,
                                       params.memoryLengthSeconds);
    const float forget = juce::jlimit (0.0f, 1.0f, params.forget);
    const float hopSec = static_cast<float> (constants::hopSize)
                         / static_cast<float> (std::max (1.0, sampleRate_));

    const float attackSec = juce::jlimit (0.02f, 0.22f, memSec * 0.03f);
    const float releaseSec = juce::jlimit (0.15f, 8.0f,
                                           memSec * (0.45f + 0.55f * (1.0f - forget * forget)));
    const float attackCoeff = 1.0f - std::exp (-hopSec / attackSec);
    const float releaseCoeff = 1.0f - std::exp (-hopSec / releaseSec);

    for (int i = 0; i < numBins; ++i)
    {
        const float broad = broadEnvScratch_[static_cast<std::size_t> (i)] + 1.0e-6f;
        const float relative = memoryMagnitudes[i] / broad;
        // Soft-compress relative prominence into a familiar target
        const float target = relative / (1.0f + relative);

        float& e = env[static_cast<std::size_t> (i)];
        if (target > e)
            e += attackCoeff * (target - e);
        else
            e += releaseCoeff * (target - e);
        e = juce::jlimit (0.0f, 1.0f, e);
        if (! std::isfinite (e))
            e = 0.0f;
    }
}

void SpectralModeProcessor::applyErasePath (float* magnitudes,
                                            const float* memoryMagnitudes,
                                            int numBins,
                                            const ModeParams& params,
                                            int channelIndex,
                                            bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || memoryMagnitudes == nullptr || numBins <= 0)
        return;

    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    if (updateSmoothers)
        updateEraseFamiliarity (memoryMagnitudes, numBins, params, channelIndex);

    const auto& familiarity = eraseFamiliarity_[ch];
    if (static_cast<int> (familiarity.size()) < numBins)
        return;

    const float mixAmount = computeMixAmount (SpectralMode::Erase, params, channelIndex, updateSmoothers);
    const float mappedInf = mapInfluenceForMode (SpectralMode::Erase, params.influence);

    // Moderate: ~-8 dB familiar; high: ~-22 dB (deeper carve on fully familiar bins)
    const float maxEraseDb = 12.0f + mappedInf * 18.0f;

    const int blurExtra = blurRadiusFromAmount (params.blur, 28);
    const int maskRadius = kEraseMaskBaseRadius + blurExtra;

    // Current relative prominence for mask comparison
    boxBlurMagnitudes (magnitudes, broadEnvScratch_.data(), numBins,
                       kEraseBroadEnvRadius, prefixScratch_.data());

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::MemoryOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = familiarity[static_cast<std::size_t> (i)];
        sanitizeMagnitudes (magnitudes, numBins);
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    for (int i = 0; i < numBins; ++i)
    {
        const float cur = magnitudes[i];
        const float broad = broadEnvScratch_[static_cast<std::size_t> (i)] + 1.0e-6f;
        const float curRel = cur / broad;
        const float curNorm = curRel / (1.0f + curRel);
        const float fam = familiarity[static_cast<std::size_t> (i)];

        const float novelBoost = std::max (0.0f, curNorm - fam) / (curNorm + fam + kEraseFamiliarityKnee);
        // Familiarity map is already relative prominence; novel energy reduces the mask.
        const float raw = juce::jlimit (0.0f, 1.0f, fam * (1.0f - 0.88f * novelBoost));
        eraseMaskScratch_[static_cast<std::size_t> (i)] = std::pow (std::max (raw, 0.0f), kEraseContrastExp);
    }

    // Blur primarily on the suppression mask
    boxBlurMagnitudes (eraseMaskScratch_.data(),
                       eraseMaskSmoothScratch_.data(),
                       numBins,
                       maskRadius,
                       prefixScratch_.data());

    auto& maskTemporal = eraseMaskSmoothed_[ch];

    for (int i = 0; i < numBins; ++i)
    {
        float mask = eraseMaskSmoothScratch_[static_cast<std::size_t> (i)];
        if (updateSmoothers)
        {
            float& mt = maskTemporal[static_cast<std::size_t> (i)];
            mt += (mask - mt) * kEraseMaskTemporalCoeff;
            mask = mt;
        }

        const float cur = magnitudes[i];
        const float attenDb = -maxEraseDb * mixAmount * juce::jlimit (0.0f, 1.0f, mask);
        float gain = constants::dbToGain (attenDb);
        gain = std::max (gain, kEraseSoftFloorLin);
        float out = cur * gain;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
        if (kDebugAudition == DebugAudition::EraseRemovedOnly)
            out = std::max (0.0f, cur - out);
        else if (kDebugAudition == DebugAudition::MergeDifferenceOnly)
            out = std::abs (cur - out);
#endif

        magnitudes[i] = out;
    }

    applyAbsoluteCeiling (magnitudes, numBins, channelIndex);
    applyPerBinContrastLimiter (magnitudes, numBins, kContrastLimitDb,
                                limiterScratch_.data(), prefixScratch_.data());
    sanitizeMagnitudes (magnitudes, numBins);
}

//==============================================================================
void SpectralModeProcessor::applyMergePath (float* magnitudes,
                                            const float* memoryMagnitudes,
                                            int numBins,
                                            const ModeParams& params,
                                            int channelIndex,
                                            bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || memoryMagnitudes == nullptr || numBins <= 0)
        return;

    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    const float mixAmount = computeMixAmount (SpectralMode::Merge, params, channelIndex, updateSmoothers);
    const float blur = juce::jlimit (0.0f, 1.0f, params.blur);

    if (mixAmount <= kInfluenceEpsilon)
        return;

    // Short current-profile EMA (~70 ms)
    auto& curProf = mergeCurrentProfile_[ch];
    const float hopSec = static_cast<float> (constants::hopSize)
                         / static_cast<float> (std::max (1.0, sampleRate_));
    const float profileMs = juce::jlimit (40.0f, 100.0f, kMergeCurrentProfileMs + blur * 30.0f);
    const float coeff = 1.0f - std::exp (-hopSec / (profileMs * 0.001f));

    if (updateSmoothers && static_cast<int> (curProf.size()) >= numBins)
    {
        bool cold = true;
        for (int i = 0; i < numBins; ++i)
        {
            if (curProf[static_cast<std::size_t> (i)] > 1.0e-8f)
            {
                cold = false;
                break;
            }
        }

        for (int i = 0; i < numBins; ++i)
        {
            float& p = curProf[static_cast<std::size_t> (i)];
            if (cold)
                p = magnitudes[i];
            else
                p += coeff * (magnitudes[i] - p);
            mergeCurProfileScratch_[static_cast<std::size_t> (i)] = p;
        }
    }
    else
    {
        std::copy (magnitudes, magnitudes + numBins, mergeCurProfileScratch_.begin());
    }

    // Envelope radius: Blur is the signature control
    const int envRadius = kMergeEnvBaseRadius + blurRadiusFromAmount (blur, 40);

    boxBlurMagnitudes (mergeCurProfileScratch_.data(), mergeCurEnvScratch_.data(),
                       numBins, envRadius, prefixScratch_.data());
    boxBlurMagnitudes (memoryMagnitudes, mergeHistEnvScratch_.data(),
                       numBins, envRadius, prefixScratch_.data());

    // Optional extra multi-pass on envelopes at high Blur
    if (blur > 0.35f)
    {
        const int extra = blurRadiusFromAmount ((blur - 0.35f) / 0.65f, 18);
        boxBlurMagnitudes (mergeCurEnvScratch_.data(), diffuseScratchA_.data(),
                           numBins, extra, prefixScratch_.data());
        std::copy (diffuseScratchA_.begin(), diffuseScratchA_.begin() + numBins,
                   mergeCurEnvScratch_.begin());
        boxBlurMagnitudes (mergeHistEnvScratch_.data(), diffuseScratchA_.data(),
                           numBins, extra, prefixScratch_.data());
        std::copy (diffuseScratchA_.begin(), diffuseScratchA_.begin() + numBins,
                   mergeHistEnvScratch_.begin());
    }

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::MemoryOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = memoryMagnitudes[i];
        sanitizeMagnitudes (magnitudes, numBins);
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    // Morph amount: Influence drives transfer; Blur widens / softens identity.
    const float morphAmount = juce::jlimit (0.0f, 1.0f, mixAmount * (0.55f + 0.45f * blur));

    for (int i = 0; i < numBins; ++i)
    {
        const float cur = magnitudes[i];
        const float curEnv = mergeCurEnvScratch_[static_cast<std::size_t> (i)] + kMergeDbEpsilon;
        const float histEnv = mergeHistEnvScratch_[static_cast<std::size_t> (i)] + kMergeDbEpsilon;

        const float curDb = constants::gainToDb (curEnv);
        const float histDb = constants::gainToDb (histEnv);
        const float mergedDb = curDb + (histDb - curDb) * morphAmount;
        const float mergedEnv = constants::dbToGain (mergedDb);

        // Fine structure from current; Blur damps narrow detail into the cloud.
        float fine = cur / curEnv;
        const float fineDamp = 1.0f - 0.55f * blur * morphAmount;
        fine = 1.0f + (fine - 1.0f) * fineDamp;

        float out = fine * mergedEnv;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
        if (kDebugAudition == DebugAudition::MergeDifferenceOnly)
            out = std::abs (out - cur);
#endif

        magnitudes[i] = out;
    }

    applyAbsoluteCeiling (magnitudes, numBins, channelIndex);
    applyPerBinContrastLimiter (magnitudes, numBins, kContrastLimitDb,
                                limiterScratch_.data(), prefixScratch_.data());
    sanitizeMagnitudes (magnitudes, numBins);
}

//==============================================================================
void SpectralModeProcessor::applyModeMagnitudes (SpectralMode mode,
                                                 float* magnitudes,
                                                 const float* phases,
                                                 const float* memoryMagnitudes,
                                                 const SpectralHistoryBuffer* history,
                                                 int numBins,
                                                 const ModeParams& params,
                                                 int channelIndex,
                                                 bool updateSmoothers,
                                                 bool leaveDryForComplexWrite) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow:
            applyShadowPath (magnitudes, phases, memoryMagnitudes, history, numBins, params,
                             channelIndex, updateSmoothers, leaveDryForComplexWrite);
            break;
        case SpectralMode::Erase:
            shadowComplexWrite_ = false;
            applyErasePath (magnitudes, memoryMagnitudes, numBins, params,
                            channelIndex, updateSmoothers);
            break;
        case SpectralMode::Merge:
            shadowComplexWrite_ = false;
            applyMergePath (magnitudes, memoryMagnitudes, numBins, params,
                            channelIndex, updateSmoothers);
            break;
    }
}

void SpectralModeProcessor::applyShadowMagnitudes (float* magnitudes,
                                                   const float* historyMagnitudes,
                                                   int numBins,
                                                   const ModeParams& params,
                                                   int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Shadow, magnitudes, nullptr, historyMagnitudes, nullptr,
                         numBins, params, channelIndex, true, false);
}

void SpectralModeProcessor::applyEraseMagnitudes (float* magnitudes,
                                                  const float* historyMagnitudes,
                                                  int numBins,
                                                  const ModeParams& params,
                                                  int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Erase, magnitudes, nullptr, historyMagnitudes, nullptr,
                         numBins, params, channelIndex, true, false);
}

void SpectralModeProcessor::applyMergeMagnitudes (float* magnitudes,
                                                  const float* historyMagnitudes,
                                                  int numBins,
                                                  const ModeParams& params,
                                                  int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Merge, magnitudes, nullptr, historyMagnitudes, nullptr,
                         numBins, params, channelIndex, true, false);
}

bool SpectralModeProcessor::process (SpectralMode mode,
                                     SpectralFrame& frame,
                                     const ModeParams& params,
                                     const float* memoryMagnitudes,
                                     const SpectralHistoryBuffer* history,
                                     int channelIndex,
                                     int hopSamples) noexcept
{
    lastMode_ = mode;
    shadowComplexWrite_ = false;

    if (channelIndex == 0)
    {
        noteModeChange (mode);
        advanceModeCrossfade (hopSamples);
    }
    else
    {
        targetMode_ = mode;
    }

    const int n = std::min (numBins_, static_cast<int> (frame.magnitudes.size()));
    if (n <= 0 || memoryMagnitudes == nullptr)
        return false;

    const float influence = juce::jlimit (0.0f, 1.0f, params.influence);
    if (influence <= kInfluenceEpsilon)
        return false;

    const bool fading = modeCrossfade_ < 1.0f - kModeAmountEpsilon
                        && previousMode_ != targetMode_;

    if (! fading)
    {
        const bool complexShadow = (targetMode_ == SpectralMode::Shadow);
        applyModeMagnitudes (targetMode_, frame.magnitudes.data(), frame.phases.data(),
                             memoryMagnitudes, history, n, params, channelIndex, true,
                             complexShadow);
        return true;
    }

    if (static_cast<int> (crossfadeScratch_.size()) < n
        || static_cast<int> (identityScratch_.size()) < n)
    {
        applyModeMagnitudes (targetMode_, frame.magnitudes.data(), frame.phases.data(),
                             memoryMagnitudes, history, n, params, channelIndex, true, false);
        return true;
    }

    std::copy (frame.magnitudes.begin(), frame.magnitudes.begin() + n, identityScratch_.begin());
    std::copy (identityScratch_.begin(), identityScratch_.begin() + n, crossfadeScratch_.begin());

    // Mode crossfade blends magnitude results; Shadow uses shared-phase fold-in here.
    applyModeMagnitudes (previousMode_, crossfadeScratch_.data(), frame.phases.data(),
                         memoryMagnitudes, history, n, params, channelIndex, false, false);
    applyModeMagnitudes (targetMode_, frame.magnitudes.data(), frame.phases.data(),
                         memoryMagnitudes, history, n, params, channelIndex, true, false);

    const float a = modeCrossfade_;
    const float b = 1.0f - a;
    for (int i = 0; i < n; ++i)
    {
        frame.magnitudes[static_cast<std::size_t> (i)] =
            frame.magnitudes[static_cast<std::size_t> (i)] * a
            + crossfadeScratch_[static_cast<std::size_t> (i)] * b;
    }

    shadowComplexWrite_ = false;
    return true;
}

} // namespace afterimage
