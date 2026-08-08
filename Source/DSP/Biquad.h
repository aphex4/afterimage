#pragma once

#include <algorithm>
#include <cmath>

namespace afterimage
{

/** RT-safe stereo peaking / shelf biquad (no heap). */
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

struct BiquadState
{
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x, const BiquadCoeffs& c) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

inline BiquadCoeffs makePeak (double sampleRate, float freq, float q, float gainLin) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float A = std::max (1.0e-6f, gainLin);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float alpha = std::sin (w0) / (2.0f * std::max (0.05f, q));
    const float cosw = std::cos (w0);

    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw;
    const float a2 = 1.0f - alpha / A;
    const float inv = 1.0f / a0;

    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

inline BiquadCoeffs makeHighShelf (double sampleRate, float freq, float q, float gainLin) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float A = std::max (1.0e-6f, gainLin);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float sinw = std::sin (w0);
    const float alpha = sinw / (2.0f * std::max (0.05f, q));
    const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;

    const float b0 =      A * ((A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAAlpha);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    const float b2 =      A * ((A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAAlpha);
    const float a0 =           (A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAAlpha;
    const float a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    const float a2 =           (A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAAlpha;
    const float inv = 1.0f / a0;

    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

inline BiquadCoeffs makeHighPass (double sampleRate, float freq, float q) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.0f * std::max (0.05f, q));

    const float b0 =  (1.0f + cosw) * 0.5f;
    const float b1 = -(1.0f + cosw);
    const float b2 =  (1.0f + cosw) * 0.5f;
    const float a0 =   1.0f + alpha;
    const float a1 =  -2.0f * cosw;
    const float a2 =   1.0f - alpha;
    const float inv = 1.0f / a0;

    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

inline BiquadCoeffs makeBandPass (double sampleRate, float freq, float q) noexcept
{
    const float f = std::clamp (freq, 20.0f, (float) sampleRate * 0.45f);
    const float w0 = (float) (2.0 * 3.14159265358979323846 * (double) f / sampleRate);
    const float cosw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.0f * std::max (0.05f, q));

    const float b0 =  alpha;
    const float b1 =  0.0f;
    const float b2 = -alpha;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cosw;
    const float a2 =  1.0f - alpha;
    const float inv = 1.0f / a0;

    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

} // namespace afterimage
