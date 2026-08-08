#include "SpectralMemoryProfile.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace afterimage
{

namespace
{
constexpr float kWeightEpsilon = 1.0e-12f;
} // namespace

float SpectralMemoryProfile::gaussianWeight (float ageOffsetFrames, float sigmaFrames) noexcept
{
    const float s = std::max (1.0e-4f, sigmaFrames);
    const float x = ageOffsetFrames / s;
    return std::exp (-0.5f * x * x);
}

void SpectralMemoryProfile::prepare (int numBins,
                                     double sampleRate,
                                     int hopSize,
                                     float maxWindowMs)
{
    numBins_ = std::max (0, numBins);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    hopSize_ = std::max (1, hopSize);

    const float hopMs = 1000.0f * static_cast<float> (hopSize_)
                        / static_cast<float> (sampleRate_);
    const float cappedMs = juce::jlimit (50.0f, 500.0f, maxWindowMs);
    maxWindowFrames_ = std::max (1, static_cast<int> (std::ceil (cappedMs / hopMs)));

    magnitudes_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    weightSum_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    weightedSum_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    weightedSumSq_.assign (static_cast<std::size_t> (numBins_), 0.0f);
    stability_.assign (static_cast<std::size_t> (numBins_), 1.0f);

    reset();
}

void SpectralMemoryProfile::reset() noexcept
{
    clear();
}

void SpectralMemoryProfile::clear() noexcept
{
    std::fill (magnitudes_.begin(), magnitudes_.end(), 0.0f);
    std::fill (weightSum_.begin(), weightSum_.end(), 0.0f);
    std::fill (weightedSum_.begin(), weightedSum_.end(), 0.0f);
    std::fill (weightedSumSq_.begin(), weightedSumSq_.end(), 0.0f);
    std::fill (stability_.begin(), stability_.end(), 1.0f);
    energy_ = 0.0f;
    framesUsed_ = 0;
    totalWeight_ = 0.0f;
}

void SpectralMemoryProfile::accumulateFrame (const SpectralFrame& frame, float weight) noexcept
{
    if (weight <= kWeightEpsilon || numBins_ <= 0)
        return;

    const int n = std::min (numBins_, static_cast<int> (frame.magnitudes.size()));
    for (int k = 0; k < n; ++k)
    {
        const float m = frame.magnitudes[static_cast<std::size_t> (k)];
        const auto i = static_cast<std::size_t> (k);
        weightedSum_[i] += weight * m;
        weightedSumSq_[i] += weight * m * m;
        weightSum_[i] += weight;
    }
    totalWeight_ += weight;
    ++framesUsed_;
}

void SpectralMemoryProfile::finalize (bool applyStability, float varianceScale) noexcept
{
    if (numBins_ <= 0)
        return;

    const float vScale = std::max (0.0f, varianceScale);

    for (int k = 0; k < numBins_; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const float w = weightSum_[i];
        if (w <= kWeightEpsilon)
        {
            magnitudes_[i] = 0.0f;
            stability_[i] = 1.0f;
            continue;
        }

        // Power-domain mean preserves energy and keeps moving partials defined.
        const float mean = std::sqrt (std::max (0.0f, weightedSumSq_[i] / w));
        float stab = 1.0f;

        if (applyStability)
        {
            // Var of linear magnitude — used as a hint only (Erase familiarity).
            const float linearMean = weightedSum_[i] / w;
            const float second = weightedSumSq_[i] / w;
            const float var = std::max (0.0f, second - linearMean * linearMean);
            const float norm = linearMean * linearMean + 1.0e-12f;
            const float normalizedVar = var / norm;
            stab = 1.0f / (1.0f + vScale * normalizedVar);
            stab = juce::jlimit (0.15f, 1.0f, stab);
        }

        stability_[i] = stab;
        // Do not attenuate magnitudes by stability — that double-penalized melodic bins.
        magnitudes_[i] = std::max (0.0f, mean);
        if (! std::isfinite (magnitudes_[i]))
            magnitudes_[i] = 0.0f;
    }

    recomputeEnergy();
}

void SpectralMemoryProfile::recomputeEnergy() noexcept
{
    double e = 0.0;
    for (int k = 0; k < numBins_; ++k)
    {
        const double m = static_cast<double> (magnitudes_[static_cast<std::size_t> (k)]);
        e += m * m;
    }
    energy_ = static_cast<float> (e);
    if (! std::isfinite (energy_))
        energy_ = 0.0f;
}

void SpectralMemoryProfile::buildFromHistory (const SpectralHistoryBuffer& history,
                                              float recallAge01,
                                              const MemoryProfileOptions& options) noexcept
{
    clear();

    const int active = history.getActiveFrameCount();
    if (active <= 0 || numBins_ <= 0)
        return;

    const float hopMs = 1000.0f * static_cast<float> (hopSize_)
                        / static_cast<float> (sampleRate_);
    const float windowMs = juce::jlimit (50.0f, static_cast<float> (maxWindowFrames_) * hopMs,
                                         options.windowMs);
    const int windowFrames = juce::jlimit (1, maxWindowFrames_,
                                           static_cast<int> (std::lround (windowMs / hopMs)));
    const int half = windowFrames / 2;

    const float centerAge = juce::jlimit (0.0f, 1.0f, recallAge01)
                            * static_cast<float> (std::max (0, active - 1));
    const float sigma = std::max (0.75f, static_cast<float> (half) * options.sigmaFraction);

    // Reset accumulators (clear already zeroed magnitudes; re-init sums)
    std::fill (weightSum_.begin(), weightSum_.end(), 0.0f);
    std::fill (weightedSum_.begin(), weightedSum_.end(), 0.0f);
    std::fill (weightedSumSq_.begin(), weightedSumSq_.end(), 0.0f);
    framesUsed_ = 0;
    totalWeight_ = 0.0f;

    for (int offset = -half; offset <= half; ++offset)
    {
        const float ageF = centerAge + static_cast<float> (offset);
        const int age = juce::jlimit (0, active - 1, static_cast<int> (std::lround (ageF)));
        const float w = gaussianWeight (static_cast<float> (offset), sigma);
        accumulateFrame (history.getFrameByAgeFrames (age), w);
    }

    finalize (options.applyStability, options.varianceScale);
}

void SpectralMemoryProfile::captureRecent (const SpectralHistoryBuffer& history,
                                           float windowMs,
                                           const MemoryProfileOptions& options) noexcept
{
    clear();

    const int active = history.getActiveFrameCount();
    if (active <= 0 || numBins_ <= 0)
        return;

    const float hopMs = 1000.0f * static_cast<float> (hopSize_)
                        / static_cast<float> (sampleRate_);
    const float winMs = juce::jlimit (50.0f, static_cast<float> (maxWindowFrames_) * hopMs, windowMs);
    const int windowFrames = juce::jlimit (1, maxWindowFrames_,
                                           static_cast<int> (std::lround (winMs / hopMs)));

    // Bias peak toward slightly older frames (~30% into the recent window).
    const float peakAge = 0.30f * static_cast<float> (windowFrames - 1);
    const float sigma = std::max (0.75f, 0.35f * static_cast<float> (windowFrames));

    std::fill (weightSum_.begin(), weightSum_.end(), 0.0f);
    std::fill (weightedSum_.begin(), weightedSum_.end(), 0.0f);
    std::fill (weightedSumSq_.begin(), weightedSumSq_.end(), 0.0f);
    framesUsed_ = 0;
    totalWeight_ = 0.0f;

    const int lastAge = std::min (active - 1, windowFrames - 1);
    for (int age = 0; age <= lastAge; ++age)
    {
        const float offset = static_cast<float> (age) - peakAge;
        const float w = gaussianWeight (offset, sigma);
        accumulateFrame (history.getFrameByAgeFrames (age), w);
    }

    MemoryProfileOptions opts = options;
    opts.windowMs = winMs;
    finalize (opts.applyStability, opts.varianceScale);
}

void SpectralMemoryProfile::copyFrom (const SpectralMemoryProfile& other) noexcept
{
    const int n = std::min (numBins_, other.numBins_);
    for (int k = 0; k < n; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        magnitudes_[i] = other.magnitudes_[i];
        stability_[i] = other.stability_[i];
    }
    for (int k = n; k < numBins_; ++k)
    {
        magnitudes_[static_cast<std::size_t> (k)] = 0.0f;
        stability_[static_cast<std::size_t> (k)] = 1.0f;
    }
    energy_ = other.energy_;
    framesUsed_ = other.framesUsed_;
    totalWeight_ = other.totalWeight_;
    if (n < numBins_)
        recomputeEnergy();
}

void SpectralMemoryProfile::lerpFrom (const SpectralMemoryProfile& a,
                                      const SpectralMemoryProfile& b,
                                      float t01) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, t01);
    const float u = 1.0f - t;
    const int n = std::min (numBins_, std::min (a.numBins_, b.numBins_));

    for (int k = 0; k < n; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        magnitudes_[i] = u * a.magnitudes_[i] + t * b.magnitudes_[i];
        stability_[i] = u * a.stability_[i] + t * b.stability_[i];
        if (! std::isfinite (magnitudes_[i]))
            magnitudes_[i] = 0.0f;
    }
    for (int k = n; k < numBins_; ++k)
    {
        magnitudes_[static_cast<std::size_t> (k)] = 0.0f;
        stability_[static_cast<std::size_t> (k)] = 1.0f;
    }

    framesUsed_ = std::max (a.framesUsed_, b.framesUsed_);
    totalWeight_ = u * a.totalWeight_ + t * b.totalWeight_;
    recomputeEnergy();
}

} // namespace afterimage
