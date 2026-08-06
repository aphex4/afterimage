#pragma once

#include "Utilities/Constants.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace afterimage
{
namespace fixtures
{

/** Deterministic synthetic magnitude spectra for Erase/Merge identity tests. */
inline void fillSaw (std::vector<float>& m, float fundamentalBin = 8.0f, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    for (int h = 1; h <= 24; ++h)
    {
        const int bin = static_cast<int> (std::lround (fundamentalBin * static_cast<float> (h)));
        if (bin <= 0 || bin >= constants::numBins)
            break;
        m[static_cast<std::size_t> (bin)] = amp / static_cast<float> (h);
    }
}

inline void fillChord (std::vector<float>& m, float rootBin = 10.0f, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    // Major triad-ish: root, ~5/4, ~3/2 (approx bin ratios)
    const float ratios[] = { 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f };
    for (float r : ratios)
    {
        for (int h = 1; h <= 8; ++h)
        {
            const int bin = static_cast<int> (std::lround (rootBin * r * static_cast<float> (h)));
            if (bin <= 0 || bin >= constants::numBins)
                break;
            auto& v = m[static_cast<std::size_t> (bin)];
            v = std::max (v, amp / static_cast<float> (h));
        }
    }
}

/** Formant-like broad resonances (vocal identity). */
inline void fillFormant (std::vector<float>& m, float f1Bin = 40.0f, float f2Bin = 120.0f, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.02f * amp);
    auto bump = [&] (float center, float width, float peak)
    {
        for (int i = 1; i < constants::numBins; ++i)
        {
            const float d = (static_cast<float> (i) - center) / width;
            m[static_cast<std::size_t> (i)] += peak * std::exp (-0.5f * d * d);
        }
    };
    bump (f1Bin, 12.0f, amp);
    bump (f2Bin, 18.0f, 0.7f * amp);
    bump (f2Bin * 1.6f, 22.0f, 0.35f * amp);
}

inline void fillKick (std::vector<float>& m, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    for (int i = 1; i < constants::numBins; ++i)
    {
        const float f = static_cast<float> (i);
        // Strong low shelf + decaying click body
        const float body = amp * std::exp (-f / 18.0f);
        const float click = amp * 0.25f * std::exp (-f / 80.0f);
        m[static_cast<std::size_t> (i)] = body + click;
    }
}

inline void fillSnare (std::vector<float>& m, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    for (int i = 1; i < constants::numBins; ++i)
    {
        const float f = static_cast<float> (i);
        const float tone = amp * 0.6f * std::exp (-std::abs (f - 55.0f) / 14.0f);
        const float noise = amp * 0.35f * std::exp (-f / 220.0f);
        m[static_cast<std::size_t> (i)] = tone + noise;
    }
}

inline void fillHat (std::vector<float>& m, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    for (int i = 1; i < constants::numBins; ++i)
    {
        const float f = static_cast<float> (i);
        // Bright noise tilt — energy above mid
        m[static_cast<std::size_t> (i)] = amp * (0.05f + 0.95f * (f / static_cast<float> (constants::numBins)))
                                          * std::exp (-f / 700.0f);
    }
}

inline void fillPinkTilt (std::vector<float>& m, float amp = 1.0f)
{
    m.assign (static_cast<std::size_t> (constants::numBins), 0.0f);
    for (int i = 1; i < constants::numBins; ++i)
        m[static_cast<std::size_t> (i)] = amp / std::sqrt (static_cast<float> (i));
}

[[nodiscard]] inline double sumSq (const float* a, int n) noexcept
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
        e += static_cast<double> (a[i]) * static_cast<double> (a[i]);
    return e;
}

[[nodiscard]] inline double logSpectralDistance (const float* a, const float* b, int n) noexcept
{
    double s = 0.0;
    constexpr float eps = 1.0e-8f;
    for (int i = 0; i < n; ++i)
    {
        const float da = constants::gainToDb (a[i] + eps);
        const float db = constants::gainToDb (b[i] + eps);
        s += static_cast<double> (std::abs (da - db));
    }
    return s / static_cast<double> (std::max (1, n));
}

/** Broad envelope via fixed-radius box blur (test-side helper). */
inline void smoothEnvelope (const float* in, float* out, int n, int radius, float* prefix)
{
    prefix[0] = 0.0f;
    for (int i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + in[i];
    for (int i = 0; i < n; ++i)
    {
        const int lo = std::max (0, i - radius);
        const int hi = std::min (n - 1, i + radius);
        out[i] = (prefix[hi + 1] - prefix[lo]) / static_cast<float> (hi - lo + 1);
    }
}

[[nodiscard]] inline double envelopeDistance (const float* a, const float* b, int n, int radius = 24)
{
    std::vector<float> ea ((size_t) n), eb ((size_t) n), prefix ((size_t) n + 1);
    smoothEnvelope (a, ea.data(), n, radius, prefix.data());
    smoothEnvelope (b, eb.data(), n, radius, prefix.data());
    return logSpectralDistance (ea.data(), eb.data(), n);
}

[[nodiscard]] inline double meanAttenuationDb (const float* before, const float* after,
                                               const std::vector<int>& bins) noexcept
{
    if (bins.empty())
        return 0.0;
    double s = 0.0;
    constexpr float eps = 1.0e-8f;
    for (int b : bins)
    {
        const float gain = (after[(size_t) b] + eps) / (before[(size_t) b] + eps);
        s += static_cast<double> (constants::gainToDb (gain));
    }
    return s / static_cast<double> (bins.size());
}

} // namespace fixtures
} // namespace afterimage
