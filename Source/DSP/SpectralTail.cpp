#include "SpectralTail.h"
#include "XorshiftRandom.h"

#include <algorithm>
#include <cmath>

namespace afterimage
{

namespace
{
constexpr float kParamEpsilon = 1.0e-4f;
constexpr float kDenormalFlush = 1.0e-12f;
constexpr float kLn1000 = 6.907755f; // ln(1000) for RT60 → per-hop decay
constexpr float kOmegaSteerRate = 0.30f;

[[nodiscard]] inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

void SpectralTail::prepare (int numBins, double sampleRate, int hopSize, int numChannels)
{
    numBins_ = std::max (0, numBins);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    hopSize_ = std::max (1, hopSize);
    numChannels_ = std::max (1, numChannels);

    const auto chN = static_cast<std::size_t> (numChannels_);
    const auto binN = static_cast<std::size_t> (numBins_);

    tailMag_.assign (chN, std::vector<float> (binN, 0.0f));
    ghostPhase_.assign (chN, std::vector<float> (binN, 0.0f));
    prevInputPhase_.assign (chN, std::vector<float> (binN, 0.0f));
    decayCoeff_.assign (chN, std::vector<float> (binN, 0.0f));
    shimmerLfo_.assign (chN, std::vector<float> (binN, 0.0f));
    tailOmega_.assign (chN, std::vector<float> (binN, 0.0f));
    rng_.assign (chN, 0u);
    cachedRt60_.assign (chN, -1.0f);
    cachedHfDamp_.assign (chN, -1.0f);
    hasPrevPhase_.assign (chN, false);

    diffuseScratch_.assign (binN, 0.0f);
    prefixScratch_.assign (binN + 1, 0.0f);
    smoother_.prepare (numBins_, sampleRate_, constants::fftSize);

    // Independent seeds per channel for stereo width under diffusion.
    if (numChannels_ >= 1)
        rng_[0] = 0xC0FFEEu ^ 0xA11CE5u;
    if (numChannels_ >= 2)
        rng_[1] = 0x5EEDBEEFu ^ 0xD1FF0u;
    for (int c = 2; c < numChannels_; ++c)
        rng_[static_cast<std::size_t> (c)] = 0x9E3779B9u * static_cast<std::uint32_t> (c + 1);

    reset();
}

void SpectralTail::reset() noexcept
{
    for (auto& v : tailMag_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : ghostPhase_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : prevInputPhase_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : shimmerLfo_)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : tailOmega_)
        std::fill (v.begin(), v.end(), 0.0f);
    std::fill (cachedRt60_.begin(), cachedRt60_.end(), -1.0f);
    std::fill (cachedHfDamp_.begin(), cachedHfDamp_.end(), -1.0f);
    std::fill (hasPrevPhase_.begin(), hasPrevPhase_.end(), false);
}

void SpectralTail::recomputeDecayCoeffs (int channelIndex, float rt60Seconds, float hfDampRatio) noexcept
{
    const auto ch = static_cast<std::size_t> (channelIndex);
    auto& coeffs = decayCoeff_[ch];
    const float hopSeconds = static_cast<float> (hopSize_)
                             / static_cast<float> (std::max (1.0, sampleRate_));
    const float rt60 = std::max (0.02f, rt60Seconds);
    const float hf = clampf (hfDampRatio, 0.05f, 1.0f);

    // Log-frequency curve anchored at 100 Hz so mid-treble (1–6 kHz) is damped.
    const float binHz = static_cast<float> (sampleRate_) / static_cast<float> (constants::fftSize);
    const float refHz = 100.0f;
    const float nyquistHz = static_cast<float> (sampleRate_) * 0.5f;
    const float logSpan = std::log2 (std::max (2.0f, nyquistHz / refHz));

    for (int k = 0; k < numBins_; ++k)
    {
        const float fHz = std::max (refHz, static_cast<float> (k) * binHz);
        const float logNorm = clampf (std::log2 (fHz / refHz) / logSpan, 0.0f, 1.0f);
        const float rt60k = rt60 * std::pow (hf, logNorm);
        coeffs[static_cast<std::size_t> (k)] =
            std::exp (-kLn1000 * hopSeconds / std::max (0.02f, rt60k));
    }

    cachedRt60_[ch] = rt60Seconds;
    cachedHfDamp_[ch] = hfDampRatio;
}

void SpectralTail::processHop (int channelIndex,
                               const float* inputMagnitudes,
                               const float* inputPhases,
                               const SpectralTailParams& params) noexcept
{
    if (inputMagnitudes == nullptr || inputPhases == nullptr || numBins_ <= 0)
        return;
    if (channelIndex < 0 || channelIndex >= numChannels_)
        return;

    const auto ch = static_cast<std::size_t> (channelIndex);
    auto& mag = tailMag_[ch];
    auto& gphase = ghostPhase_[ch];
    auto& prevPh = prevInputPhase_[ch];
    auto& shimmer = shimmerLfo_[ch];
    auto& omegaHeld = tailOmega_[ch];
    auto& coeffs = decayCoeff_[ch];
    auto& rng = rng_[ch];

    if (std::abs (params.rt60Seconds - cachedRt60_[ch]) > kParamEpsilon
        || std::abs (params.hfDampRatio - cachedHfDamp_[ch]) > kParamEpsilon)
    {
        recomputeDecayCoeffs (channelIndex, params.rt60Seconds, params.hfDampRatio);
    }

    constexpr float twoPi = 6.28318530717958647692f;
    constexpr float pi = 3.14159265358979323846f;
    const float fft = static_cast<float> (constants::fftSize);
    const float hop = static_cast<float> (hopSize_);
    const bool hadPrev = hasPrevPhase_[ch];

    float frameMax = 0.0f;
    for (int k = 0; k < numBins_; ++k)
        frameMax = std::max (frameMax, inputMagnitudes[k]);
    const float frameThreshold = frameMax * 0.005f;

    for (int k = 0; k < numBins_; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const float inMag = std::max (0.0f, inputMagnitudes[k]);
        const float inPh = inputPhases[k];

        // (a) Held omega: only steer from phase-vocoder when bin has real energy
        const float expected = twoPi * static_cast<float> (k) * hop / fft;
        const bool significant = hadPrev && (inMag > frameThreshold);

        if (significant)
        {
            const float dphi = princarg (inPh - prevPh[i] - expected);
            const float measured = expected + dphi;
            const float steer = clampf (params.injectGain * kOmegaSteerRate, 0.0f, 1.0f);
            omegaHeld[i] += steer * princarg (measured - omegaHeld[i]);
        }
        else if (omegaHeld[i] == 0.0f)
        {
            omegaHeld[i] = expected;
        }

        const float omega = omegaHeld[i];
        prevPh[i] = inPh;

        // (b)/(c) Frequency-dependent decay + accumulate
        const float decay = coeffs[i];
        const float d = params.freeze ? 1.0f : decay;
        const float inject = params.freeze ? 0.0f : params.injectGain * (1.0f - decay);

        const float prevMag = mag[i];
        mag[i] = prevMag * d + inMag * inject;
        if (mag[i] < kDenormalFlush)
            mag[i] = 0.0f;

        // (e) Initialise ghost phase from input on first injection
        if (prevMag < kDenormalFlush && mag[i] >= kDenormalFlush)
            gphase[i] = inPh;

        // (d) Advance + diffuse ghost phase
        shimmer[i] += 0.002f * (nextBipolarRandom (rng) - shimmer[i]);
        const float detune = 1.0f + params.shimmerCents * shimmer[i] * 0.0005787f;
        gphase[i] += omega * detune;

        if (params.diffusion > 1.0e-4f)
            gphase[i] += params.diffusion * pi * nextGaussianApprox (rng);

        gphase[i] = princarg (gphase[i]);
    }

    // Cross-bin constant-Q diffusion on the accumulator (compounds across hops).
    if (params.spectralDiffusion > 1.0e-4f
        && static_cast<int> (diffuseScratch_.size()) >= numBins_
        && static_cast<int> (prefixScratch_.size()) >= numBins_ + 1)
    {
        smoother_.setWidth (params.diffusionOctaves);
        smoother_.process (mag.data(), diffuseScratch_.data(), numBins_, prefixScratch_.data());

        const float s = clampf (params.spectralDiffusion, 0.0f, 1.0f);
        for (int k = 0; k < numBins_; ++k)
        {
            const auto i = static_cast<std::size_t> (k);
            mag[i] = mag[i] * (1.0f - s) + diffuseScratch_[i] * s;
        }
    }

    hasPrevPhase_[ch] = true;
}

const float* SpectralTail::getTailMagnitudes (int channelIndex) const noexcept
{
    if (tailMag_.empty() || channelIndex < 0 || channelIndex >= numChannels_)
        return nullptr;
    return tailMag_[static_cast<std::size_t> (channelIndex)].data();
}

const float* SpectralTail::getTailPhases (int channelIndex) const noexcept
{
    if (ghostPhase_.empty() || channelIndex < 0 || channelIndex >= numChannels_)
        return nullptr;
    return ghostPhase_[static_cast<std::size_t> (channelIndex)].data();
}

} // namespace afterimage
