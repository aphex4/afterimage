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

enum class EqChannelMode : int
{
    Stereo = 0,
    LeftRight,
    MidSide
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
    Eight-band parametric EQ — last creative stage before Gain Match.
    Stereo / LR (independent state, linked controls) / Mid-Side.
    Solo audits a single band. ×4 cascades LP/HP stages for steeper slopes.
*/
class ParametricEQ
{
public:
    static constexpr int kBands = constants::parametricEqBands;
    static constexpr int kMaxStages = 4; // ×4 steepness

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        maxBlock_ = juce::jmax (1, maxBlock);
        midScratch_.setSize (1, maxBlock_, false, true, true);
        sideScratch_.setSize (1, maxBlock_, false, true, true);
        leftScratch_.setSize (1, maxBlock_, false, true, true);
        rightScratch_.setSize (1, maxBlock_, false, true, true);
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

    void setChannelMode (EqChannelMode mode) noexcept { channelMode_ = mode; }

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
        if (needsRebuild)
            updateBandCoeffs (index);
    }

    [[nodiscard]] const EqBandParams& getBand (int index) const noexcept
    {
        return bands_[static_cast<size_t> (juce::jlimit (0, kBands - 1, index))];
    }

    [[nodiscard]] const SpectrumProbe& getProbe() const noexcept { return probe_; }

    /** Magnitude response in dB at hz for curve drawing (UI thread). */
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
                // H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
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
        const int chans = juce::jmin (buffer.getNumChannels(), 2);
        if (n <= 0 || chans <= 0)
            return;

        const int soloIndex = findSoloBand();

        if (channelMode_ == EqChannelMode::MidSide && chans == 2)
        {
            for (int i = 0; i < n; ++i)
            {
                const float l = buffer.getSample (0, i);
                const float r = buffer.getSample (1, i);
                midScratch_.setSample (0, i, 0.5f * (l + r));
                sideScratch_.setSample (0, i, 0.5f * (l - r));
            }
            processMono (midScratch_.getWritePointer (0), n, 0, soloIndex);
            processMono (sideScratch_.getWritePointer (0), n, 1, soloIndex);
            for (int i = 0; i < n; ++i)
            {
                const float m = midScratch_.getSample (0, i);
                const float s = sideScratch_.getSample (0, i);
                buffer.setSample (0, i, m + s);
                buffer.setSample (1, i, m - s);
            }
        }
        else if (channelMode_ == EqChannelMode::LeftRight && chans == 2)
        {
            processMono (buffer.getWritePointer (0), n, 0, soloIndex);
            processMono (buffer.getWritePointer (1), n, 1, soloIndex);
        }
        else
        {
            // Stereo-linked: process L, copy state path 0→1 by processing both with path 0 coeffs/state separately
            for (int ch = 0; ch < chans; ++ch)
                processMono (buffer.getWritePointer (ch), n, ch, soloIndex);
        }

        const float* l = buffer.getReadPointer (0);
        const float* r = chans > 1 ? buffer.getReadPointer (1) : nullptr;
        probe_.process (l, r, n);
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
            if (bands_[static_cast<size_t> (b)].solo && bands_[static_cast<size_t> (b)].enabled)
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
            case EqFilterType::LowPass:  return makeLowPass (sampleRate_, band.freqHz, band.q);
            case EqFilterType::HighPass: return makeHighPass (sampleRate_, band.freqHz, band.q);
            case EqFilterType::LowShelf: return makeLowShelf (sampleRate_, band.freqHz, band.q, g);
            case EqFilterType::HighShelf:return makeHighShelf (sampleRate_, band.freqHz, band.q, g);
            case EqFilterType::Notch:    return makeNotch (sampleRate_, band.freqHz, band.q);
            case EqFilterType::Bell:
            default:                     return makePeak (sampleRate_, band.freqHz, band.q, g);
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
                // Audition: band-affected signal only (filtered wet of that band)
                const int b = soloIndex;
                const auto& band = bands_[static_cast<size_t> (b)];
                const int stages = numStages (band);
                float s = x;
                for (int st = 0; st < stages; ++st)
                    s = states_[static_cast<size_t> (path)][static_cast<size_t> (b)][static_cast<size_t> (st)]
                            .process (s, coeffs_[static_cast<size_t> (b)][static_cast<size_t> (st)]);
                // For boost/cut audition use wet-dry of that stage
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
    EqChannelMode channelMode_ = EqChannelMode::Stereo;
    std::array<EqBandParams, kBands> bands_ {};
    std::array<std::array<BiquadCoeffs, kMaxStages>, kBands> coeffs_ {};
    // [path L/M or R/S][band][stage]
    std::array<std::array<std::array<BiquadState, kMaxStages>, kBands>, 2> states_ {};
    juce::AudioBuffer<float> midScratch_, sideScratch_, leftScratch_, rightScratch_;
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
