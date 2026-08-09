#pragma once

#include "Biquad.h"
#include "SpectrumProbe.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <complex>

namespace afterimage
{

enum class EqFilterType : int
{
    LowPass = 0,
    HighPass,
    LowShelf,
    HighShelf,
    Bell,
    Notch,
    NumTypes
};

struct EqBandParams
{
    bool enabled = false;
    EqFilterType type = EqFilterType::Bell;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.7f;
    bool x4 = false;
    bool solo = false;
};

inline BiquadCoeffs makeLowPass (double sampleRate, float freq, float q) noexcept;
inline BiquadCoeffs makeLowShelf (double sampleRate, float freq, float q, float gainLin) noexcept;
inline BiquadCoeffs makeNotch (double sampleRate, float freq, float q) noexcept;

/**
    Eight-band stereo-linked parametric EQ — last creative stage before Gain Match.
    Exclusive solo (at most one band). ×4 cascades LP/HP stages.
    RT-safe stack biquads; skip process when inactive.
*/
class ParametricEQ
{
public:
    static constexpr int kBands = constants::parametricEqBands;
    static constexpr int kMaxStages = 4;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        maxBlock_ = juce::jmax (1, maxBlock);
        probe_.prepare (sampleRate_, maxBlock_);
        reset();
        for (int b = 0; b < kBands; ++b)
        {
            bands_[static_cast<size_t> (b)].freqHz = constants::kDefaultParaEqFreqs[b];
            updateBandCoeffs (b);
        }
    }

    void reset() noexcept
    {
        for (auto& path : states_)
            for (auto& band : path)
                for (auto& st : band)
                    st.reset();
        probe_.reset();
    }

    void setMasterEnabled (bool on) noexcept { masterEnabled_ = on; }
    [[nodiscard]] bool isMasterEnabled() const noexcept { return masterEnabled_; }

    void setBand (int index, const EqBandParams& p) noexcept
    {
        if (index < 0 || index >= kBands)
            return;

        auto& dst = bands_[static_cast<size_t> (index)];
        const bool needsRebuild = dst.type != p.type || dst.x4 != p.x4
            || std::abs (dst.freqHz - p.freqHz) > 0.25f
            || std::abs (dst.gainDb - p.gainDb) > 0.05f
            || std::abs (dst.q - p.q) > 0.01f;

        dst = p;
        dst.freqHz = juce::jlimit (20.0f, 20000.0f, dst.freqHz);
        dst.gainDb = juce::jlimit (-24.0f, 24.0f, dst.gainDb);
        dst.q = juce::jlimit (0.1f, 20.0f, dst.q);

        // Exclusive solo: newly soloed band clears others.
        if (dst.solo)
        {
            for (int b = 0; b < kBands; ++b)
                if (b != index)
                    bands_[static_cast<size_t> (b)].solo = false;
        }

        if (needsRebuild)
            updateBandCoeffs (index);
    }

    /** Apply exclusive solo from APVTS (multiple may be true — keep lowest index). */
    void applyExclusiveSoloFromFlags() noexcept
    {
        int first = -1;
        for (int b = 0; b < kBands; ++b)
        {
            if (bands_[static_cast<size_t> (b)].solo)
            {
                if (first < 0)
                    first = b;
                else
                    bands_[static_cast<size_t> (b)].solo = false;
            }
        }
    }

    [[nodiscard]] const EqBandParams& getBand (int index) const noexcept
    {
        return bands_[static_cast<size_t> (juce::jlimit (0, kBands - 1, index))];
    }

    [[nodiscard]] const SpectrumProbe& getProbe() const noexcept { return probe_; }

    /** Feed pre-EQ analyzer (call before process, even when EQ inactive). */
    void pushSpectrum (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = buffer.getNumChannels();
        if (n <= 0 || chans <= 0)
            return;
        const float* l = buffer.getReadPointer (0);
        const float* r = chans > 1 ? buffer.getReadPointer (1) : nullptr;
        probe_.process (l, r, n);
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        if (! masterEnabled_)
            return false;
        if (findSoloBand() >= 0)
            return true;
        for (int b = 0; b < kBands; ++b)
            if (bands_[static_cast<size_t> (b)].enabled)
                return true;
        return false;
    }

    [[nodiscard]] float responseDbAt (float hz) const noexcept
    {
        std::complex<double> h (1.0, 0.0);
        const double w = 2.0 * 3.14159265358979323846 * (double) hz / sampleRate_;
        const std::complex<double> z = { std::cos (w), -std::sin (w) };
        const std::complex<double> z2 = z * z;

        for (int b = 0; b < kBands; ++b)
        {
            const auto& band = bands_[static_cast<size_t> (b)];
            if (! band.enabled && ! band.solo)
                continue;
            const int stages = numStages (band);
            for (int s = 0; s < stages; ++s)
            {
                const auto& c = coeffs_[static_cast<size_t> (b)][static_cast<size_t> (s)];
                const std::complex<double> num = (double) c.b0 + (double) c.b1 * z + (double) c.b2 * z2;
                const std::complex<double> den = 1.0 + (double) c.a1 * z + (double) c.a2 * z2;
                if (std::abs (den) > 1.0e-12)
                    h *= num / den;
            }
        }
        const double mag = std::abs (h);
        return (float) (20.0 * std::log10 (std::max (mag, 1.0e-8)));
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        if (n <= 0 || chans <= 0 || ! isActive())
            return;

        const int soloIndex = findSoloBand();
        for (int ch = 0; ch < chans; ++ch)
            processMono (buffer.getWritePointer (ch), n, ch, soloIndex);
    }

private:
    static int numStages (const EqBandParams& b) noexcept
    {
        if ((b.type == EqFilterType::LowPass || b.type == EqFilterType::HighPass) && b.x4)
            return 4;
        return 1;
    }

    int findSoloBand() const noexcept
    {
        for (int b = 0; b < kBands; ++b)
            if (bands_[static_cast<size_t> (b)].solo)
                return b;
        return -1;
    }

    void updateBandCoeffs (int index) noexcept
    {
        auto& band = bands_[static_cast<size_t> (index)];
        const int stages = numStages (band);
        for (int s = 0; s < kMaxStages; ++s)
        {
            auto& c = coeffs_[static_cast<size_t> (index)][static_cast<size_t> (s)];
            if (s >= stages)
            {
                c = { 1, 0, 0, 0, 0 };
                continue;
            }
            c = makeCoeffs (band);
        }
    }

    BiquadCoeffs makeCoeffs (const EqBandParams& band) const noexcept
    {
        const float g = juce::Decibels::decibelsToGain (band.gainDb);
        switch (band.type)
        {
            case EqFilterType::LowPass:   return makeLowPass (sampleRate_, band.freqHz, band.q);
            case EqFilterType::HighPass:  return makeHighPass (sampleRate_, band.freqHz, band.q);
            case EqFilterType::LowShelf:  return makeLowShelf (sampleRate_, band.freqHz, band.q, g);
            case EqFilterType::HighShelf: return makeHighShelf (sampleRate_, band.freqHz, band.q, g);
            case EqFilterType::Notch:     return makeNotch (sampleRate_, band.freqHz, band.q);
            case EqFilterType::Bell:
            default:                      return makePeak (sampleRate_, band.freqHz, band.q, g);
        }
    }

    void processMono (float* data, int n, int path, int soloIndex) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float x = data[i];
            float y = x;

            if (soloIndex >= 0)
            {
                const int b = soloIndex;
                const auto& band = bands_[static_cast<size_t> (b)];
                const int stages = numStages (band);
                float s = x;
                for (int st = 0; st < stages; ++st)
                    s = states_[static_cast<size_t> (path)][static_cast<size_t> (b)][static_cast<size_t> (st)]
                            .process (s, coeffs_[static_cast<size_t> (b)][static_cast<size_t> (st)]);
                y = s;
            }
            else
            {
                for (int b = 0; b < kBands; ++b)
                {
                    const auto& band = bands_[static_cast<size_t> (b)];
                    if (! band.enabled)
                        continue;
                    const int stages = numStages (band);
                    for (int st = 0; st < stages; ++st)
                        y = states_[static_cast<size_t> (path)][static_cast<size_t> (b)][static_cast<size_t> (st)]
                                .process (y, coeffs_[static_cast<size_t> (b)][static_cast<size_t> (st)]);
                }
            }
            data[i] = y;
        }
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    int maxBlock_ = 512;
    bool masterEnabled_ = false;
    std::array<EqBandParams, kBands> bands_ {};
    std::array<std::array<BiquadCoeffs, kMaxStages>, kBands> coeffs_ {};
    std::array<std::array<std::array<BiquadState, kMaxStages>, kBands>, 2> states_ {};
    SpectrumProbe probe_;
};

inline BiquadCoeffs makeLowPass (double sampleRate, float freq, float q) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.0f * std::max (0.05f, q));
    const float b0 = (1.0f - cosw) * 0.5f;
    const float b1 =  1.0f - cosw;
    const float b2 = (1.0f - cosw) * 0.5f;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cosw;
    const float a2 =  1.0f - alpha;
    const float inv = 1.0f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

inline BiquadCoeffs makeLowShelf (double sampleRate, float freq, float q, float gainLin) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float A = std::max (1.0e-6f, gainLin);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float sinw = std::sin (w0);
    const float alpha = sinw / (2.0f * std::max (0.05f, q));
    const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;
    const float b0 =      A * ((A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAAlpha);
    const float b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    const float b2 =      A * ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAAlpha);
    const float a0 =           (A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAAlpha;
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw);
    const float a2 =           (A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAAlpha;
    const float inv = 1.0f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

inline BiquadCoeffs makeNotch (double sampleRate, float freq, float q) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.0f * std::max (0.05f, q));
    const float b0 =  1.0f;
    const float b1 = -2.0f * cosw;
    const float b2 =  1.0f;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cosw;
    const float a2 =  1.0f - alpha;
    const float inv = 1.0f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

} // namespace afterimage
