#include "LogSmoother.h"

#include <algorithm>
#include <cmath>

namespace afterimage
{

void LogSmoother::prepare (int numBins, double sampleRate, int fftSize)
{
    numBins_ = std::max (0, numBins);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    fftSize_ = std::max (1, fftSize);

    const auto n = static_cast<std::size_t> (numBins_);
    loIndex_.assign (n, 0);
    hiIndex_.assign (n, 0);
    invCount_.assign (n, 1.0f);

    width_ = -1.0f;
    cachedQuantizedWidth_ = -1.0f;
    setWidth (0.0f);
}

void LogSmoother::rebuildTables (float octaveFraction) noexcept
{
    if (numBins_ <= 0)
        return;

    const float oct = std::max (0.0f, octaveFraction);
    if (oct < 1.0e-6f)
    {
        for (int k = 0; k < numBins_; ++k)
        {
            loIndex_[static_cast<std::size_t> (k)] = k;
            hiIndex_[static_cast<std::size_t> (k)] = k;
            invCount_[static_cast<std::size_t> (k)] = 1.0f;
        }
        return;
    }

    const float halfRatio = std::pow (2.0f, oct * 0.5f) - 1.0f;
    const int last = numBins_ - 1;

    for (int k = 0; k < numBins_; ++k)
    {
        const int halfWidth = std::max (1, static_cast<int> (std::lround (
            static_cast<double> (k) * static_cast<double> (halfRatio))));
        const int lo = std::max (0, k - halfWidth);
        const int hi = std::min (last, k + halfWidth);
        loIndex_[static_cast<std::size_t> (k)] = lo;
        hiIndex_[static_cast<std::size_t> (k)] = hi;
        invCount_[static_cast<std::size_t> (k)] = 1.0f / static_cast<float> (hi - lo + 1);
    }
}

void LogSmoother::setWidth (float octaveFraction) noexcept
{
    const float w = std::max (0.0f, octaveFraction);
    // Quantise to ~64 steps so setWidth from the audio thread stays cheap.
    const float q = (w <= 1.0e-6f) ? 0.0f
                                   : (std::round (w * static_cast<float> (kWidthQuantSteps))
                                      / static_cast<float> (kWidthQuantSteps));

    if (std::abs (q - cachedQuantizedWidth_) < 1.0e-6f && width_ >= 0.0f)
    {
        width_ = w;
        return;
    }

    rebuildTables (q);
    cachedQuantizedWidth_ = q;
    width_ = w;
}

void LogSmoother::process (const float* input, float* output, int numBins, float* prefixScratch) noexcept
{
    if (input == nullptr || output == nullptr || numBins <= 0)
        return;

    const int n = std::min (numBins, numBins_);
    if (n <= 0)
        return;

    if (cachedQuantizedWidth_ < 1.0e-6f || prefixScratch == nullptr)
    {
        if (output != input)
            std::copy (input, input + n, output);
        return;
    }

    prefixScratch[0] = 0.0f;
    for (int i = 0; i < n; ++i)
        prefixScratch[i + 1] = prefixScratch[i] + input[i];

    for (int i = 0; i < n; ++i)
    {
        const int lo = loIndex_[static_cast<std::size_t> (i)];
        const int hi = std::min (hiIndex_[static_cast<std::size_t> (i)], n - 1);
        output[i] = (prefixScratch[hi + 1] - prefixScratch[lo])
                    * invCount_[static_cast<std::size_t> (i)];
    }
}

} // namespace afterimage
