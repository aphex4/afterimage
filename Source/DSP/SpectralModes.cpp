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
constexpr float kEnergyCompDbLimit = 6.0f;
constexpr float kEnergySmoothCoeff = 0.25f;
constexpr float kTransientSmoothCoeff = 0.35f;
constexpr float kEraseMaxAtten = 0.92f;
constexpr float kOverlapEpsilon = 1.0e-8f;
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

    energyScaleSmoothed_.assign (static_cast<std::size_t> (numChannels_), 1.0f);
    transientSmoothed_.assign (static_cast<std::size_t> (numChannels_), 0.0f);

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
    std::fill (energyScaleSmoothed_.begin(), energyScaleSmoothed_.end(), 1.0f);
    std::fill (transientSmoothed_.begin(), transientSmoothed_.end(), 0.0f);
}

void SpectralModeProcessor::setMode (SpectralMode mode) noexcept
{
    noteModeChange (mode);
}

void SpectralModeProcessor::noteModeChange (SpectralMode mode) noexcept
{
    if (mode == targetMode_)
        return;

    // Mid-fade retarget: treat the mode we were heading toward as the new "from".
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
    juce::ignoreUnused (need);
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
        return blurScratch_.data();
    }
    return historyMagnitudes;
}

float SpectralModeProcessor::computeMixAmount (const ModeParams& params,
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
    const float influence = juce::jlimit (0.0f, 1.0f, params.influence);
    float effectiveInfluence = influence * (1.0f - trSmooth * preserve);
    effectiveInfluence = juce::jlimit (0.0f, 1.0f, effectiveInfluence);

    const float historyWeight = ageWeightFromForget (params.recallAge01, params.forget);
    return effectiveInfluence * historyWeight;
}

void SpectralModeProcessor::applyEnergyCompensation (float* magnitudes,
                                                     int numBins,
                                                     double energyIn,
                                                     double energyOut,
                                                     int channelIndex,
                                                     bool updateSmoothers) noexcept
{
    ensureChannelState (channelIndex);
    const auto ch = static_cast<std::size_t> (juce::jlimit (0, numChannels_ - 1, channelIndex));

    float targetScale = 1.0f;
    if (energyOut > static_cast<double> (kEnergyEpsilon)
        && energyIn > static_cast<double> (kEnergyEpsilon))
    {
        const float raw = static_cast<float> (std::sqrt (energyIn / energyOut));
        const float db = juce::jlimit (-kEnergyCompDbLimit,
                                       kEnergyCompDbLimit,
                                       constants::gainToDb (std::max (raw, 1.0e-8f)));
        targetScale = constants::dbToGain (db);
    }

    float scale = energyScaleSmoothed_[ch];
    if (updateSmoothers)
    {
        scale += (targetScale - scale) * kEnergySmoothCoeff;
        scale = juce::jlimit (constants::dbToGain (-kEnergyCompDbLimit),
                              constants::dbToGain (kEnergyCompDbLimit),
                              scale);
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

void SpectralModeProcessor::applyModeMagnitudes (SpectralMode mode,
                                                 float* magnitudes,
                                                 const float* historyMagnitudes,
                                                 int numBins,
                                                 const ModeParams& params,
                                                 int channelIndex,
                                                 bool updateSmoothers) noexcept
{
    if (magnitudes == nullptr || historyMagnitudes == nullptr || numBins <= 0)
        return;

    const float* histSrc = prepareBlurredHistory (historyMagnitudes, numBins, params.blur);
    const float mixAmount = computeMixAmount (params, channelIndex, updateSmoothers);

    double energyIn = 0.0;
    double energyOut = 0.0;

    switch (mode)
    {
        case SpectralMode::Shadow:
        {
            for (int i = 0; i < numBins; ++i)
            {
                const float cur = magnitudes[i];
                const float out = cur + histSrc[i] * mixAmount;
                magnitudes[i] = out;
                energyIn += static_cast<double> (cur) * static_cast<double> (cur);
                energyOut += static_cast<double> (out) * static_cast<double> (out);
            }
            break;
        }

        case SpectralMode::Erase:
        {
            for (int i = 0; i < numBins; ++i)
            {
                const float cur = magnitudes[i];
                const float hist = histSrc[i];
                const float overlap = hist / (hist + cur + kOverlapEpsilon);
                float atten = mixAmount * overlap;
                atten = std::min (atten, kEraseMaxAtten);
                const float out = cur * (1.0f - atten);
                magnitudes[i] = out;
                energyIn += static_cast<double> (cur) * static_cast<double> (cur);
                energyOut += static_cast<double> (out) * static_cast<double> (out);
            }
            break;
        }

        case SpectralMode::Merge:
        {
            for (int i = 0; i < numBins; ++i)
            {
                const float cur = magnitudes[i];
                const float hist = histSrc[i];
                const float out = cur + (hist - cur) * mixAmount;
                magnitudes[i] = out;
                energyIn += static_cast<double> (cur) * static_cast<double> (cur);
                energyOut += static_cast<double> (out) * static_cast<double> (out);
            }
            break;
        }
    }

    applyEnergyCompensation (magnitudes, numBins, energyIn, energyOut, channelIndex, updateSmoothers);
    sanitizeMagnitudes (magnitudes, numBins);
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
        // Keep target in sync for stereo without double-advancing the fade.
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

    // Dual-pass crossfade: previous → target.
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
