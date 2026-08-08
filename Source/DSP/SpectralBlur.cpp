#include "SpectralBlur.h"
#include "XorshiftRandom.h"

#include <algorithm>
#include <cmath>

namespace afterimage
{

namespace
{
constexpr float kDenormalFlush = 1.0e-20f;
constexpr float kOmegaSteerRate = 0.30f;

[[nodiscard]] inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

void SpectralBlur::prepare (int numBins, double sampleRate, int hopSize, int numChannels)
{
    numBins_ = std::max (0, numBins);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    hopSize_ = std::max (1, hopSize);
    numChannels_ = std::max (1, numChannels);

    const auto chN = static_cast<std::size_t> (numChannels_);
    const auto binN = static_cast<std::size_t> (numBins_);

    smearPow_.assign (chN, std::vector<float> (binN, 0.0f));
    blurMag_.assign (chN, std::vector<float> (binN, 0.0f));
    blurPhase_.assign (chN, std::vector<float> (binN, 0.0f));
    renderPhase_.assign (chN, std::vector<float> (binN, 0.0f));
    blurOmega_.assign (chN, std::vector<float> (binN, 0.0f));
    prevInputPhase_.assign (chN, std::vector<float> (binN, 0.0f));
    rng_.assign (chN, 0u);
    primed_.assign (chN, false);
    hasPrevPhase_.assign (chN, false);

    smearScratch_.assign (binN, 0.0f);
    prefixScratch_.assign (binN + 1, 0.0f);
    smoother_.prepare (numBins_, sampleRate_, constants::fftSize);

    // Independent seeds per channel for stereo decorrelation under phase scatter.
    if (numChannels_ >= 1)
        rng_[0] = 0xB1A7B1u ^ 0x51A55u;
    if (numChannels_ >= 2)
        rng_[1] = 0xDEC0DEAu ^ 0xF00Du;
    for (int c = 2; c < numChannels_; ++c)
        rng_[static_cast<std::size_t> (c)] = 0x9E3779B9u * static_cast<std::uint32_t> (c + 17);

    reset();
}

void SpectralBlur::reset() noexcept
{
    for (auto& v : smearPow_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : blurMag_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : blurPhase_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : renderPhase_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : blurOmega_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : prevInputPhase_)
        std::fill (v.begin(), v.end(), 0.0f);
    std::fill (primed_.begin(), primed_.end(), false);
    std::fill (hasPrevPhase_.begin(), hasPrevPhase_.end(), false);
}

void SpectralBlur::processHop (int channelIndex,
                               const float* inputMagnitudes,
                               const float* inputPhases,
                               const float* memoryMagnitudes,
                               const SpectralBlurParams& params) noexcept
{
    if (inputMagnitudes == nullptr || inputPhases == nullptr || numBins_ <= 0)
        return;
    if (channelIndex < 0 || channelIndex >= numChannels_)
        return;

    const auto ch = static_cast<std::size_t> (channelIndex);
    auto& pow = smearPow_[ch];
    auto& mag = blurMag_[ch];
    auto& bphase = blurPhase_[ch];
    auto& rphase = renderPhase_[ch];
    auto& prevPh = prevInputPhase_[ch];
    auto& omegaHeld = blurOmega_[ch];
    auto& rng = rng_[ch];
    const bool hadPrev = hasPrevPhase_[ch];
    const bool wasPrimed = primed_[ch];

    constexpr float twoPi = 6.28318530717958647692f;
    constexpr float pi = 3.14159265358979323846f;
    const float fft = static_cast<float> (constants::fftSize);
    const float hop = static_cast<float> (hopSize_);
    const float hopSec = hop / static_cast<float> (std::max (1.0, sampleRate_));
    const float blend = clampf (params.memoryBlend, 0.0f, 1.0f);
    const float coeff = 1.0f - std::exp (-hopSec / std::max (0.001f, params.timeSmearMs * 0.001f));

    float frameMax = 0.0f;
    for (int k = 0; k < numBins_; ++k)
        frameMax = std::max (frameMax, inputMagnitudes[k]);
    const float frameThreshold = frameMax * 0.005f;

    // A1 — power-domain temporal EMA (skip updates while frozen once primed).
    for (int k = 0; k < numBins_; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const float inMag = std::max (0.0f, inputMagnitudes[k]);
        const float memMag = (memoryMagnitudes != nullptr)
                                 ? std::max (0.0f, memoryMagnitudes[k])
                                 : inMag;
        const float srcMag = (memoryMagnitudes != nullptr)
                                 ? inMag * (1.0f - blend) + memMag * blend
                                 : inMag;
        const float srcPow = srcMag * srcMag;

        float& p = pow[i];
        if (! wasPrimed)
            p = srcPow;
        else if (! params.freeze)
            p += coeff * (srcPow - p);

        if (p < kDenormalFlush)
            p = 0.0f;

        mag[i] = std::sqrt (p);
    }
    primed_[ch] = true;

    // A2 — narrow constant-Q frequency smear (garnish; keep ≤ ~0.3 octaves typical).
    if (params.freqSmearOctaves > 1.0e-3f
        && static_cast<int> (smearScratch_.size()) >= numBins_
        && static_cast<int> (prefixScratch_.size()) >= numBins_ + 1)
    {
        smoother_.setWidth (params.freqSmearOctaves);
        smoother_.process (mag.data(), smearScratch_.data(), numBins_, prefixScratch_.data());
        std::copy (smearScratch_.begin(), smearScratch_.begin() + numBins_, mag.begin());
    }

    // A3/A4 — held omega, clean phase propagation, render-time scatter (not accumulated).
    for (int k = 0; k < numBins_; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const float inMag = std::max (0.0f, inputMagnitudes[k]);
        const float inPh = inputPhases[k];
        const float expected = twoPi * static_cast<float> (k) * hop / fft;

        if (hadPrev && inMag > frameThreshold)
        {
            const float measured = expected + princarg (inPh - prevPh[i] - expected);
            omegaHeld[i] += kOmegaSteerRate * princarg (measured - omegaHeld[i]);
        }
        else if (omegaHeld[i] == 0.0f)
        {
            omegaHeld[i] = expected;
        }

        prevPh[i] = inPh;
        bphase[i] = princarg (bphase[i] + omegaHeld[i]);

        float offset = 0.0f;
        if (params.phaseScatter > 1.0e-4f)
            offset = params.phaseScatter * pi * nextGaussianApprox (rng) * 2.0f;

        rphase[i] = princarg (bphase[i] + offset);
    }

    hasPrevPhase_[ch] = true;
}

const float* SpectralBlur::getBlurMagnitudes (int channelIndex) const noexcept
{
    if (blurMag_.empty() || channelIndex < 0 || channelIndex >= numChannels_)
        return nullptr;
    return blurMag_[static_cast<std::size_t> (channelIndex)].data();
}

const float* SpectralBlur::getBlurPhases (int channelIndex) const noexcept
{
    if (renderPhase_.empty() || channelIndex < 0 || channelIndex >= numChannels_)
        return nullptr;
    return renderPhase_[static_cast<std::size_t> (channelIndex)].data();
}

} // namespace afterimage
