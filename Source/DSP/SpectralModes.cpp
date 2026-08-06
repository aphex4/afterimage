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
constexpr float kEnergySmoothCoeff = 0.22f;
constexpr float kHistMakeupSmoothCoeff = 0.18f;
constexpr float kTransientSmoothCoeff = 0.35f;
constexpr float kHistMakeupDbMax = 9.0f;
constexpr float kHistAttenDbMax = 6.0f;
constexpr float kSilenceEnergyThresh = 1.0e-10f;

// Erase familiarity / suppression
constexpr float kEraseFamiliarityKnee = 0.12f;
constexpr float kEraseContrastExp = 1.25f;
constexpr float kEraseMaskTemporalCoeff = 0.72f;
constexpr float kEraseSoftFloorLin = 0.02f; // ~-34 dB soft floor
constexpr int   kEraseMaskBaseRadius = 1;

// Merge envelope / landmarks
constexpr float kMergeDbEpsilon = 1.0e-8f;
constexpr float kMergeLandmarkThresh = 1.55f;
constexpr float kMergeLandmarkStrength = 0.55f;
constexpr float kMergeMaxLandmarkDb = 9.0f;
constexpr int   kMergeEnvBaseRadius = 10;
constexpr int   kMergeLandmarkSmoothRadius = 2;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
constexpr DebugAudition kDebugAudition = static_cast<DebugAudition> (AFTERIMAGE_DEBUG_AUDITION);
#else
constexpr DebugAudition kDebugAudition = DebugAudition::Normal;
#endif

[[nodiscard]] float influenceExponentForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow: return 1.70f;
        case SpectralMode::Erase:  return 2.05f;
        case SpectralMode::Merge:  return 1.45f;
    }
    return 1.70f;
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
        case SpectralMode::Shadow: return 0.20f;
        case SpectralMode::Erase:  return 0.15f;
        case SpectralMode::Merge:  return 0.25f;
    }
    return 0.20f;
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
    histNormScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);

    eraseMaskScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    eraseMaskSmoothScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeCurEnvScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeHistEnvScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeLandmarkScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    mergeOutScratch_.assign (static_cast<std::size_t> (numBins_), 0.0f);

    energyScaleSmoothed_.assign (static_cast<std::size_t> (numChannels_), 1.0f);
    histMakeupSmoothed_.assign (static_cast<std::size_t> (numChannels_), 1.0f);
    transientSmoothed_.assign (static_cast<std::size_t> (numChannels_), 0.0f);

    eraseFamiliarity_.assign (static_cast<std::size_t> (numChannels_),
                              std::vector<float> (static_cast<std::size_t> (numBins_), 0.0f));
    eraseMaskSmoothed_.assign (static_cast<std::size_t> (numChannels_),
                               std::vector<float> (static_cast<std::size_t> (numBins_), 0.0f));

    reset();
}

void SpectralModeProcessor::reset()
{
    lastMode_ = SpectralMode::Shadow;
    targetMode_ = SpectralMode::Shadow;
    previousMode_ = SpectralMode::Shadow;
    modeCrossfade_ = 1.0f;

    std::fill (blurScratch_.begin(), blurScratch_.end(), 0.0f);
    std::fill (prefixScratch_.begin(), prefixScratch_.end(), 0.0f);
    std::fill (identityScratch_.begin(), identityScratch_.end(), 0.0f);
    std::fill (crossfadeScratch_.begin(), crossfadeScratch_.end(), 0.0f);
    std::fill (histNormScratch_.begin(), histNormScratch_.end(), 0.0f);
    std::fill (eraseMaskScratch_.begin(), eraseMaskScratch_.end(), 0.0f);
    std::fill (eraseMaskSmoothScratch_.begin(), eraseMaskSmoothScratch_.end(), 0.0f);
    std::fill (mergeCurEnvScratch_.begin(), mergeCurEnvScratch_.end(), 0.0f);
    std::fill (mergeHistEnvScratch_.begin(), mergeHistEnvScratch_.end(), 0.0f);
    std::fill (mergeLandmarkScratch_.begin(), mergeLandmarkScratch_.end(), 0.0f);
    std::fill (mergeOutScratch_.begin(), mergeOutScratch_.end(), 0.0f);
    std::fill (energyScaleSmoothed_.begin(), energyScaleSmoothed_.end(), 1.0f);
    std::fill (histMakeupSmoothed_.begin(), histMakeupSmoothed_.end(), 1.0f);
    std::fill (transientSmoothed_.begin(), transientSmoothed_.end(), 0.0f);

    clearEraseMemory();
}

void SpectralModeProcessor::clearEraseMemory() noexcept
{
    for (auto& env : eraseFamiliarity_)
        std::fill (env.begin(), env.end(), 0.0f);
    for (auto& m : eraseMaskSmoothed_)
        std::fill (m.begin(), m.end(), 0.0f);
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
    jassert (need <= energyScaleSmoothed_.size());
    jassert (need <= transientSmoothed_.size());
    jassert (need <= histMakeupSmoothed_.size());
    jassert (need <= eraseFamiliarity_.size());
    jassert (need <= eraseMaskSmoothed_.size());
    juce::ignoreUnused (need);
}

float SpectralModeProcessor::getLastEnergyScale (int channelIndex) const noexcept
{
    if (energyScaleSmoothed_.empty())
        return 1.0f;
    const auto ch = static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (energyScaleSmoothed_.size()) - 1, channelIndex));
    return energyScaleSmoothed_[ch];
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

const float* SpectralModeProcessor::prepareBlurredHistory (const float* historyMagnitudes,
                                                           int numBins,
                                                           float blur01) noexcept
{
    const int radius = blurRadiusFromAmount (blur01);
    if (radius > 0 && static_cast<int> (blurScratch_.size()) >= numBins
        && static_cast<int> (prefixScratch_.size()) >= numBins + 1)
    {
        boxBlurMagnitudes (historyMagnitudes,
                           blurScratch_.data(),
                           numBins,
                           radius,
                           prefixScratch_.data());

        // Preserve RMS energy so Blur does not act as unintended attenuation.
        const double eIn = sumSq (historyMagnitudes, numBins);
        const double eOut = sumSq (blurScratch_.data(), numBins);
        if (eIn > static_cast<double> (kSilenceEnergyThresh)
            && eOut > static_cast<double> (kEnergyEpsilon))
        {
            const float scale = static_cast<float> (std::sqrt (eIn / eOut));
            const float clamped = juce::jlimit (0.5f, 2.0f, scale);
            for (int i = 0; i < numBins; ++i)
                blurScratch_[static_cast<std::size_t> (i)] *= clamped;
        }
        return blurScratch_.data();
    }
    return historyMagnitudes;
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
    float effectiveInfluence = mapped * (1.0f - juce::jlimit (0.0f, 1.0f, reduction));
    effectiveInfluence = juce::jlimit (0.0f, 1.0f, effectiveInfluence);

    const float historyWeight = remappedHistoryWeight (mode, params.recallAge01, params.forget);
    return effectiveInfluence * historyWeight;
}

const float* SpectralModeProcessor::normalizeHistoryEnergy (SpectralMode mode,
                                                            const float* historyMagnitudes,
                                                            const float* currentMagnitudes,
                                                            int numBins,
                                                            int channelIndex,
                                                            bool updateSmoothers) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    if (historyMagnitudes == nullptr || currentMagnitudes == nullptr || numBins <= 0
        || static_cast<int> (histNormScratch_.size()) < numBins)
        return historyMagnitudes;

    const double curE = sumSq (currentMagnitudes, numBins);
    const double histE = sumSq (historyMagnitudes, numBins);

    float targetScale = 1.0f;
    if (histE > static_cast<double> (kSilenceEnergyThresh)
        && curE > static_cast<double> (kSilenceEnergyThresh))
    {
        // Mode-specific target ratio of history RMS vs current RMS.
        float targetRatio = 0.75f;
        switch (mode)
        {
            case SpectralMode::Shadow: targetRatio = 0.78f; break;
            case SpectralMode::Erase:  targetRatio = 0.90f; break; // unused by Erase path
            case SpectralMode::Merge:  targetRatio = 0.85f; break; // unused by Merge path
        }

        const float raw = static_cast<float> (std::sqrt ((curE * static_cast<double> (targetRatio)) / histE));
        const float db = juce::jlimit (-kHistAttenDbMax, kHistMakeupDbMax,
                                       constants::gainToDb (std::max (raw, 1.0e-8f)));
        targetScale = constants::dbToGain (db);
    }
    else if (histE <= static_cast<double> (kSilenceEnergyThresh))
    {
        // Near-silent history: do not amplify noise.
        targetScale = 1.0f;
        if (updateSmoothers)
            histMakeupSmoothed_[ch] = 1.0f;
        return historyMagnitudes;
    }

    float scale = histMakeupSmoothed_[ch];
    if (updateSmoothers)
    {
        scale += (targetScale - scale) * kHistMakeupSmoothCoeff;
        scale = juce::jlimit (constants::dbToGain (-kHistAttenDbMax),
                              constants::dbToGain (kHistMakeupDbMax),
                              scale);
        histMakeupSmoothed_[ch] = scale;
    }

    if (std::abs (scale - 1.0f) <= 1.0e-5f)
        return historyMagnitudes;

    for (int i = 0; i < numBins; ++i)
        histNormScratch_[static_cast<std::size_t> (i)] = historyMagnitudes[i] * scale;
    return histNormScratch_.data();
}

void SpectralModeProcessor::applyEnergyPolicy (SpectralMode mode,
                                               float* magnitudes,
                                               int numBins,
                                               double energyIn,
                                               double energyOut,
                                               float mappedInfluence,
                                               int channelIndex,
                                               bool updateSmoothers) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    float targetScale = 1.0f;
    const float mapped = juce::jlimit (0.0f, 1.0f, mappedInfluence);

    if (energyOut > static_cast<double> (kEnergyEpsilon)
        && energyIn > static_cast<double> (kEnergyEpsilon))
    {
        const float outOverIn = static_cast<float> (std::sqrt (energyOut / energyIn));

        switch (mode)
        {
            case SpectralMode::Shadow:
            {
                // Allow controlled loudness rise; only attenuate above the allowance.
                const float allowedDb = mapped * 3.0f; // 0..+3 dB
                const float allowedLin = constants::dbToGain (allowedDb);
                if (outOverIn > allowedLin)
                    targetScale = allowedLin / outOverIn;
                else
                    targetScale = 1.0f; // do not force energy back down to input
                targetScale = juce::jlimit (constants::dbToGain (-6.0f), 1.0f, targetScale);
                break;
            }

            case SpectralMode::Erase:
            {
                // Do not restore carved energy upward; soft-cap extreme drops only.
                if (outOverIn < 0.12f)
                    targetScale = 0.12f / outOverIn;
                else
                    targetScale = 1.0f;
                targetScale = juce::jlimit (1.0f, constants::dbToGain (2.0f), targetScale);
                break;
            }

            case SpectralMode::Merge:
            {
                // Keep morph audible but hold loudness near input (±~2.5 dB soft).
                const float raw = 1.0f / outOverIn;
                const float db = juce::jlimit (-2.5f, 2.5f,
                                               constants::gainToDb (std::max (raw, 1.0e-8f)));
                targetScale = constants::dbToGain (db * 0.80f);
                break;
            }
        }
    }

    float scale = energyScaleSmoothed_[ch];
    if (updateSmoothers)
    {
        scale += (targetScale - scale) * kEnergySmoothCoeff;
        scale = juce::jlimit (0.25f, 4.0f, scale);
        energyScaleSmoothed_[ch] = scale;
    }

    if (std::abs (scale - 1.0f) > 1.0e-6f)
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
void SpectralModeProcessor::updateEraseFamiliarity (const float* historyMagnitudes,
                                                    int numBins,
                                                    const ModeParams& params,
                                                    int channelIndex) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));
    auto& env = eraseFamiliarity_[ch];
    if (static_cast<int> (env.size()) < numBins || historyMagnitudes == nullptr)
        return;

    // Memory Length ≈ persistence horizon; Forget accelerates release (fade of familiarity).
    const float memSec = juce::jlimit (constants::memoryLengthMinSec,
                                       constants::memoryLengthMaxSec,
                                       params.memoryLengthSeconds);
    const float forget = juce::jlimit (0.0f, 1.0f, params.forget);
    const float hopSec = static_cast<float> (constants::hopSize)
                         / static_cast<float> (std::max (1.0, sampleRate_));

    // Fast attack (becomes familiar quickly), slow release scaled by Memory / Forget.
    const float attackSec = juce::jlimit (0.015f, 0.18f, memSec * 0.025f);
    const float releaseSec = juce::jlimit (0.12f, 8.0f,
                                           memSec * (0.40f + 0.60f * (1.0f - forget * forget)));
    const float attackCoeff = 1.0f - std::exp (-hopSec / attackSec);
    const float releaseCoeff = 1.0f - std::exp (-hopSec / releaseSec);

    // Recall age softens which historical lookback feeds familiarity (not mix depth).
    const float ageW = remappedHistoryWeight (SpectralMode::Erase, params.recallAge01, 0.0f);

    for (int i = 0; i < numBins; ++i)
    {
        const float target = historyMagnitudes[i] * (0.35f + 0.65f * ageW);
        float& e = env[static_cast<std::size_t> (i)];
        if (target > e)
            e += attackCoeff * (target - e);
        else
            e += releaseCoeff * (target - e);
        e = std::max (0.0f, e);
        if (! std::isfinite (e))
            e = 0.0f;
    }
}

void SpectralModeProcessor::applyErasePath (float* magnitudes,
                                            const float* historyMagnitudes,
                                            int numBins,
                                            const ModeParams& params,
                                            int channelIndex,
                                            bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || historyMagnitudes == nullptr || numBins <= 0)
        return;

    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    // Freeze: stop envelope updates; keep applying frozen familiarity stencil.
    if (updateSmoothers && ! params.freeze)
        updateEraseFamiliarity (historyMagnitudes, numBins, params, channelIndex);

    const auto& familiarity = eraseFamiliarity_[ch];
    if (static_cast<int> (familiarity.size()) < numBins)
        return;

    const float mixAmount = computeMixAmount (SpectralMode::Erase, params, channelIndex, updateSmoothers);
    const float mappedInf = mapInfluenceForMode (SpectralMode::Erase, params.influence);

    // Max carve depth scales with Influence (~14 dB mid → ~34 dB extreme on fully familiar).
    const float maxEraseDb = 14.0f + mappedInf * 20.0f;

    // Mask radius: base selective + Blur widens erasure regions.
    const int blurExtra = blurRadiusFromAmount (params.blur, 28);
    const int maskRadius = kEraseMaskBaseRadius + blurExtra;

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::RecalledOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = familiarity[static_cast<std::size_t> (i)];
        sanitizeMagnitudes (magnitudes, numBins);
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    // Contrast-sensitive mask: suppress where familiarity is present unless current
    // clearly exceeds the envelope (novel energy). Equal/repeated → deep carve.
    for (int i = 0; i < numBins; ++i)
    {
        const float cur = magnitudes[i];
        const float fam = familiarity[static_cast<std::size_t> (i)];
        const float famPresence = fam / (fam + kEraseFamiliarityKnee);
        const float currentExcess = std::max (0.0f, cur - fam) / (cur + fam + kEraseFamiliarityKnee);
        const float raw = juce::jlimit (0.0f, 1.0f, famPresence * (1.0f - 0.90f * currentExcess));
        eraseMaskScratch_[static_cast<std::size_t> (i)] = std::pow (raw, kEraseContrastExp);
    }

    // Spatial smooth of mask (separate from history blur).
    boxBlurMagnitudes (eraseMaskScratch_.data(),
                       eraseMaskSmoothScratch_.data(),
                       numBins,
                       maskRadius,
                       prefixScratch_.data());

    auto& maskTemporal = eraseMaskSmoothed_[ch];
    double energyIn = 0.0;
    double energyOut = 0.0;

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
        if (kDebugAudition == DebugAudition::EraseMask)
            out = std::max (0.0f, cur - out); // material being removed
        else if (kDebugAudition == DebugAudition::SpectralDelta)
            out = std::abs (cur - out);
#endif

        magnitudes[i] = out;
        energyIn += static_cast<double> (cur) * static_cast<double> (cur);
        energyOut += static_cast<double> (out) * static_cast<double> (out);
    }

    applyEnergyPolicy (SpectralMode::Erase, magnitudes, numBins, energyIn, energyOut, mappedInf,
                       channelIndex, updateSmoothers);
    sanitizeMagnitudes (magnitudes, numBins);
}

void SpectralModeProcessor::applyMergePath (float* magnitudes,
                                            const float* historyMagnitudes,
                                            int numBins,
                                            const ModeParams& params,
                                            int channelIndex,
                                            bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || historyMagnitudes == nullptr || numBins <= 0)
        return;

    ensureChannelState (channelIndex);

    const float mixAmount = computeMixAmount (SpectralMode::Merge, params, channelIndex, updateSmoothers);
    const float mappedInf = mapInfluenceForMode (SpectralMode::Merge, params.influence);

    // Blur widens historical envelope transfer (broadness of identity).
    const int envRadius = kMergeEnvBaseRadius + blurRadiusFromAmount (params.blur, 36);

    boxBlurMagnitudes (magnitudes, mergeCurEnvScratch_.data(), numBins, envRadius, prefixScratch_.data());
    boxBlurMagnitudes (historyMagnitudes, mergeHistEnvScratch_.data(), numBins, envRadius, prefixScratch_.data());

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::RecalledOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = historyMagnitudes[i];
        sanitizeMagnitudes (magnitudes, numBins);
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    // Transfer clamp scales with Influence (±6 → ±24 dB).
    const float transferLimitDb = 6.0f + mappedInf * 18.0f;

    // Landmark prominence mask from history vs its envelope.
    for (int i = 0; i < numBins; ++i)
    {
        const float hist = historyMagnitudes[i];
        const float env = mergeHistEnvScratch_[static_cast<std::size_t> (i)] + kMergeDbEpsilon;
        const float prominence = hist / env;
        float landmarkDb = 0.0f;
        if (prominence > kMergeLandmarkThresh)
        {
            landmarkDb = juce::jlimit (0.0f, kMergeMaxLandmarkDb,
                                       constants::gainToDb (std::max (prominence, 1.0e-8f)));
        }
        mergeLandmarkScratch_[static_cast<std::size_t> (i)] = landmarkDb;
    }

    boxBlurMagnitudes (mergeLandmarkScratch_.data(),
                       eraseMaskSmoothScratch_.data(), // reuse scratch
                       numBins,
                       kMergeLandmarkSmoothRadius,
                       prefixScratch_.data());

    double energyIn = 0.0;
    double energyOut = 0.0;

    for (int i = 0; i < numBins; ++i)
    {
        const float cur = magnitudes[i];
        const float curEnv = mergeCurEnvScratch_[static_cast<std::size_t> (i)];
        const float histEnv = mergeHistEnvScratch_[static_cast<std::size_t> (i)];

        const float curDb = constants::gainToDb (curEnv + kMergeDbEpsilon);
        const float histDb = constants::gainToDb (histEnv + kMergeDbEpsilon);
        float transferDb = histDb - curDb;
        transferDb = juce::jlimit (-transferLimitDb, transferLimitDb, transferDb);

        const float landmarkDb = eraseMaskSmoothScratch_[static_cast<std::size_t> (i)]
                                 * kMergeLandmarkStrength;

        // Carrier = current; reshape with envelope transfer + selective landmarks.
        const float totalDb = (transferDb + landmarkDb) * mixAmount;
        float out = cur * constants::dbToGain (totalDb);

#if defined (AFTERIMAGE_DEBUG_AUDITION)
        if (kDebugAudition == DebugAudition::MergeTransfer)
            out = cur * constants::dbToGain (std::abs (totalDb));
        else if (kDebugAudition == DebugAudition::SpectralDelta)
            out = std::abs (out - cur);
#endif

        magnitudes[i] = out;
        energyIn += static_cast<double> (cur) * static_cast<double> (cur);
        energyOut += static_cast<double> (out) * static_cast<double> (out);
    }

    applyEnergyPolicy (SpectralMode::Merge, magnitudes, numBins, energyIn, energyOut, mappedInf,
                       channelIndex, updateSmoothers);
    sanitizeMagnitudes (magnitudes, numBins);
}

void SpectralModeProcessor::applyShadowPath (float* magnitudes,
                                             const float* historyMagnitudes,
                                             int numBins,
                                             const ModeParams& params,
                                             int channelIndex,
                                             bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || historyMagnitudes == nullptr || numBins <= 0)
        return;

    const float* histSrc = prepareBlurredHistory (historyMagnitudes, numBins, params.blur);
    histSrc = normalizeHistoryEnergy (SpectralMode::Shadow, histSrc, magnitudes,
                                      numBins, channelIndex, updateSmoothers);

    const float mixAmount = computeMixAmount (SpectralMode::Shadow, params, channelIndex, updateSmoothers);
    const float mappedInf = mapInfluenceForMode (SpectralMode::Shadow, params.influence);

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::RecalledOnly)
    {
        for (int i = 0; i < numBins; ++i)
            magnitudes[i] = histSrc[i];
        sanitizeMagnitudes (magnitudes, numBins);
        return;
    }
#else
    juce::ignoreUnused (kDebugAudition);
#endif

    double energyIn = 0.0;
    double energyOut = 0.0;

    for (int i = 0; i < numBins; ++i)
    {
        const float cur = magnitudes[i];
        const float ghost = histSrc[i] * mixAmount;
        const float out = cur + ghost;
        magnitudes[i] = out;
        energyIn += static_cast<double> (cur) * static_cast<double> (cur);
        energyOut += static_cast<double> (out) * static_cast<double> (out);
    }

#if defined (AFTERIMAGE_DEBUG_AUDITION)
    if (kDebugAudition == DebugAudition::SpectralDelta)
    {
        // Leave magnitudes as processed (Shadow delta vs identity already additive).
    }
#endif

    applyEnergyPolicy (SpectralMode::Shadow, magnitudes, numBins, energyIn, energyOut, mappedInf,
                       channelIndex, updateSmoothers);
    sanitizeMagnitudes (magnitudes, numBins);
}

void SpectralModeProcessor::applyModeMagnitudes (SpectralMode mode,
                                                 float* magnitudes,
                                                 const float* historyMagnitudes,
                                                 int numBins,
                                                 const ModeParams& params,
                                                 int channelIndex,
                                                 bool updateSmoothers) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow:
            applyShadowPath (magnitudes, historyMagnitudes, numBins, params, channelIndex, updateSmoothers);
            break;
        case SpectralMode::Erase:
            applyErasePath (magnitudes, historyMagnitudes, numBins, params, channelIndex, updateSmoothers);
            break;
        case SpectralMode::Merge:
            applyMergePath (magnitudes, historyMagnitudes, numBins, params, channelIndex, updateSmoothers);
            break;
    }
}

void SpectralModeProcessor::applyShadowMagnitudes (float* magnitudes,
                                                   const float* historyMagnitudes,
                                                   int numBins,
                                                   const ModeParams& params,
                                                   int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Shadow, magnitudes, historyMagnitudes,
                         numBins, params, channelIndex, true);
}

void SpectralModeProcessor::applyEraseMagnitudes (float* magnitudes,
                                                  const float* historyMagnitudes,
                                                  int numBins,
                                                  const ModeParams& params,
                                                  int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Erase, magnitudes, historyMagnitudes,
                         numBins, params, channelIndex, true);
}

void SpectralModeProcessor::applyMergeMagnitudes (float* magnitudes,
                                                  const float* historyMagnitudes,
                                                  int numBins,
                                                  const ModeParams& params,
                                                  int channelIndex) noexcept
{
    applyModeMagnitudes (SpectralMode::Merge, magnitudes, historyMagnitudes,
                         numBins, params, channelIndex, true);
}

bool SpectralModeProcessor::process (SpectralMode mode,
                                     SpectralFrame& frame,
                                     const ModeParams& params,
                                     const float* historyMagnitudes,
                                     int channelIndex,
                                     int hopSamples) noexcept
{
    lastMode_ = mode;

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
    if (n <= 0 || historyMagnitudes == nullptr)
        return false;

    const float influence = juce::jlimit (0.0f, 1.0f, params.influence);
    if (influence <= kInfluenceEpsilon)
        return false;

    const bool fading = modeCrossfade_ < 1.0f - kModeAmountEpsilon
                        && previousMode_ != targetMode_;

    if (! fading)
    {
        applyModeMagnitudes (targetMode_, frame.magnitudes.data(), historyMagnitudes,
                             n, params, channelIndex, true);
        return true;
    }

    if (static_cast<int> (crossfadeScratch_.size()) < n
        || static_cast<int> (identityScratch_.size()) < n)
    {
        applyModeMagnitudes (targetMode_, frame.magnitudes.data(), historyMagnitudes,
                             n, params, channelIndex, true);
        return true;
    }

    std::copy (frame.magnitudes.begin(), frame.magnitudes.begin() + n, identityScratch_.begin());
    std::copy (identityScratch_.begin(), identityScratch_.begin() + n, crossfadeScratch_.begin());

    applyModeMagnitudes (previousMode_, crossfadeScratch_.data(), historyMagnitudes,
                         n, params, channelIndex, false);
    applyModeMagnitudes (targetMode_, frame.magnitudes.data(), historyMagnitudes,
                         n, params, channelIndex, true);

    const float a = modeCrossfade_;
    const float b = 1.0f - a;
    for (int i = 0; i < n; ++i)
    {
        frame.magnitudes[static_cast<std::size_t> (i)] =
            frame.magnitudes[static_cast<std::size_t> (i)] * a
            + crossfadeScratch_[static_cast<std::size_t> (i)] * b;
    }

    return true;
}

} // namespace afterimage
