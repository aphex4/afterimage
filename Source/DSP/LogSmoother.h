#pragma once

#include <vector>

namespace afterimage
{

/**
    Constant-Q (log-frequency) magnitude smoothing via prefix sums.

    Tables sized in prepare(); setWidth may recompute index tables on the
    audio thread only when the quantized width changes (no allocation).
*/
class LogSmoother
{
public:
    void prepare (int numBins, double sampleRate, int fftSize);

    /** octaveFraction: 1.0 = 1 octave, 0.166 ≈ 1/6 octave, 0 = passthrough. */
    void setWidth (float octaveFraction) noexcept;

    void process (const float* input, float* output, int numBins, float* prefixScratch) noexcept;

    [[nodiscard]] float getWidth() const noexcept { return width_; }
    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

private:
    void rebuildTables (float octaveFraction) noexcept;

    int numBins_ = 0;
    double sampleRate_ = 44100.0;
    int fftSize_ = 4096;

    std::vector<int> loIndex_;
    std::vector<int> hiIndex_;
    std::vector<float> invCount_;

    float width_ = -1.0f;
    float cachedQuantizedWidth_ = -1.0f;

    static constexpr int kWidthQuantSteps = 64;
};

} // namespace afterimage
